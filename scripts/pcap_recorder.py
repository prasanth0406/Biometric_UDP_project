#!/usr/bin/env python3
"""
scripts/pcap_recorder.py - Transparent TCP capture proxy that generates valid .pcap files
"""
import sys
import socket
import struct
import time
import threading
import os

PCAP_MAGIC = 0xa1b2c3d4
PCAP_VERSION_MAJOR = 2
PCAP_VERSION_MINOR = 4
LINKTYPE_ETHERNET = 1

def checksum(data):
    if len(data) % 2 != 0:
        data += b'\x00'
    s = sum(struct.unpack(f"!{len(data)//2}H", data))
    while (s >> 16):
        s = (s & 0xffff) + (s >> 16)
    return (~s) & 0xffff

def make_eth_ip_tcp(src_ip, dst_ip, src_port, dst_port, seq, ack, flags, payload):
    # Ethernet header (14 bytes)
    eth = b'\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x02\x08\x00'

    # IP header (20 bytes)
    ip_total_len = 20 + 20 + len(payload)
    src_ip_bytes = socket.inet_aton(src_ip)
    dst_ip_bytes = socket.inet_aton(dst_ip)
    ip_hdr_no_cksum = struct.pack("!BBHHHBBH4s4s",
                                  0x45, 0, ip_total_len, 54321, 0, 64, 6, 0,
                                  src_ip_bytes, dst_ip_bytes)
    ip_cksum = checksum(ip_hdr_no_cksum)
    ip_hdr = struct.pack("!BBHHHBBH4s4s",
                         0x45, 0, ip_total_len, 54321, 0, 64, 6, ip_cksum,
                         src_ip_bytes, dst_ip_bytes)

    # TCP header (20 bytes)
    offset_res = (5 << 4)
    tcp_hdr_no_cksum = struct.pack("!HHIIBBHHH",
                                   src_port, dst_port, seq, ack, offset_res, flags, 65535, 0, 0)
    # Pseudo header for TCP checksum
    pseudo = struct.pack("!4s4sBBH", src_ip_bytes, dst_ip_bytes, 0, 6, 20 + len(payload))
    tcp_cksum = checksum(pseudo + tcp_hdr_no_cksum + payload)
    tcp_hdr = struct.pack("!HHIIBBHHH",
                          src_port, dst_port, seq, ack, offset_res, flags, 65535, tcp_cksum, 0)

    return eth + ip_hdr + tcp_hdr + payload

class PcapWriter:
    def __init__(self, filename):
        self.filename = filename
        self.f = open(filename, "wb")
        # Global header: magic, major, minor, thiszone, sigfigs, snaplen, network
        self.f.write(struct.pack("!IHHiIII", PCAP_MAGIC, PCAP_VERSION_MAJOR, PCAP_VERSION_MINOR, 0, 0, 65535, LINKTYPE_ETHERNET))
        self.lock = threading.Lock()
        self.pkt_count = 0

    def write_packet(self, pkt_bytes):
        with self.lock:
            now = time.time()
            sec = int(now)
            usec = int((now - sec) * 1000000)
            length = len(pkt_bytes)
            hdr = struct.pack("!IIII", sec, usec, length, length)
            self.f.write(hdr + pkt_bytes)
            self.f.flush()
            self.pkt_count += 1

    def close(self):
        with self.lock:
            self.f.close()

def main():
    if len(sys.argv) < 4:
        print("Usage: python pcap_recorder.py <listen_port> <target_port> <output.pcap>")
        sys.exit(1)

    listen_port = int(sys.argv[1])
    target_port = int(sys.argv[2])
    output_pcap = sys.argv[3]

    os.makedirs(os.path.dirname(os.path.abspath(output_pcap)), exist_ok=True)
    pcap = PcapWriter(output_pcap)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", listen_port))
    server.listen(1)
    print(f"[RECORDER] Listening on 127.0.0.1:{listen_port}, forwarding to :{target_port}, output={output_pcap}")
    sys.stdout.flush()

    try:
        client_sock, client_addr = server.accept()
    except Exception as e:
        print(f"[RECORDER] Server accept error: {e}")
        pcap.close()
        server.close()
        return

    c_ip, c_port = client_addr
    target_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    target_sock.connect(("127.0.0.1", target_port))

    # Handshake simulation packets in PCAP
    seq_c = 1000
    seq_s = 2000
    # 1. SYN
    pkt_syn = make_eth_ip_tcp(c_ip, "127.0.0.1", c_port, target_port, seq_c, 0, 0x02, b'')
    pcap.write_packet(pkt_syn)
    # 2. SYN-ACK
    pkt_synack = make_eth_ip_tcp("127.0.0.1", c_ip, target_port, c_port, seq_s, seq_c + 1, 0x12, b'')
    pcap.write_packet(pkt_synack)
    # 3. ACK
    pkt_ack = make_eth_ip_tcp(c_ip, "127.0.0.1", c_port, target_port, seq_c + 1, seq_s + 1, 0x10, b'')
    pcap.write_packet(pkt_ack)

    seq_c += 1
    seq_s += 1

    def forward(src, dst, is_c2s):
        nonlocal seq_c, seq_s
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                dst.sendall(data)
                if is_c2s:
                    pkt = make_eth_ip_tcp(c_ip, "127.0.0.1", c_port, target_port, seq_c, seq_s, 0x18, data)
                    seq_c += len(data)
                else:
                    pkt = make_eth_ip_tcp("127.0.0.1", c_ip, target_port, c_port, seq_s, seq_c, 0x18, data)
                    seq_s += len(data)
                pcap.write_packet(pkt)
        except Exception:
            pass
        finally:
            try: dst.shutdown(socket.SHUT_WR)
            except Exception: pass

    t1 = threading.Thread(target=forward, args=(client_sock, target_sock, True))
    t2 = threading.Thread(target=forward, args=(target_sock, client_sock, False))
    t1.start()
    t2.start()
    t1.join()
    t2.join()

    # FIN teardown in PCAP
    pkt_fin1 = make_eth_ip_tcp(c_ip, "127.0.0.1", c_port, target_port, seq_c, seq_s, 0x11, b'')
    pcap.write_packet(pkt_fin1)
    pkt_fin2 = make_eth_ip_tcp("127.0.0.1", c_ip, target_port, c_port, seq_s, seq_c + 1, 0x11, b'')
    pcap.write_packet(pkt_fin2)

    client_sock.close()
    target_sock.close()
    server.close()
    pcap.close()
    print(f"[RECORDER] Capture complete. Recorded {pcap.pkt_count} packets to {output_pcap}")

if __name__ == "__main__":
    main()
