import socket
import base64
import hashlib
import struct

def test_ws():
    host = "192.168.12.223"
    port = 8888
    
    # WebSocket handshake
    key = base64.b64encode(b"test_key_123456").decode()
    request = (
        "GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n"
    )
    
    print(f"Connecting to {host}:{port}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(10)
    try:
        s.connect((host, port))
    except Exception as e:
        print(f"Failed to connect: {e}")
        return
    
    print("Sending handshake...")
    s.send(request.encode())
    
    response = b""
    while b"\r\n\r\n" not in response:
        chunk = s.recv(4096)
        if not chunk: break
        response += chunk
    
    header_text = response.split(b"\r\n\r\n")[0].decode()
    print("Received Handshake Response:")
    print(header_text)
    
    if "101 Switching Protocols" not in header_text:
        print("Handshake failed!")
        return

    print("\nReading binary frames...")
    try:
        while True:
            # Read WS header
            head = s.recv(2)
            if not head: 
                print("Connection closed by peer.")
                break
            
            b1, b2 = head[0], head[1]
            opcode = b1 & 0x0F
            payload_len = b2 & 0x7F
            
            if payload_len == 126:
                payload_len = struct.unpack(">H", s.recv(2))[0]
            elif payload_len == 127:
                payload_len = struct.unpack(">Q", s.recv(8))[0]
            
            # Read payload
            payload = b""
            while len(payload) < payload_len:
                chunk = s.recv(payload_len - len(payload))
                if not chunk: break
                payload += chunk
            
            print(f"Received frame: Opcode={opcode}, Length={payload_len}")
            
            # Check for marker
            if payload_len == 24:
                v1, v2, v3 = struct.unpack("<QQQ", payload)
                if v1 == 0xFFFFFFFFFFFFFFFF:
                    print("Received termination marker (3x UINT64_MAX). Test Successful.")
                    # Keep reading a bit more to see if it closes
                    continue
            
            # Decode records
            if payload_len % 28 == 0:
                num_records = payload_len // 28
                print(f"  - Contains {num_records} log records")
                # Peek at the first record in chunk
                ts, tx, rx, h = struct.unpack("<QQQI", payload[:28])
                print(f"  - Sample Record: TS={ts}, TX={tx}, RX={rx}, Hour={h}")

    except Exception as e:
        print(f"Error during streaming: {e}")
    finally:
        s.close()
        print("Socket closed.")

if __name__ == "__main__":
    test_ws()