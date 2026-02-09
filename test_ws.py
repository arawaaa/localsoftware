import socket
import base64
import hashlib

def test_handshake():
    host = '192.168.12.223'
    port = 8888
    key = "dGhlIHNhbXBsZSBub25jZQ=="
    
    request = (
        "GET / HTTP/1.1\r\n" +
        "Host: " + host + ":" + str(port) + "\r\n" +
        "Upgrade: websocket\r\n" +
        "Connection: Upgrade\r\n" +
        "Sec-WebSocket-Key: " + key + "\r\n" +
        "Sec-WebSocket-Version: 13\r\n" +
        "\r\n"
    )
    
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        try:
            s.connect((host, port))
            s.sendall(request.encode())
            response = s.recv(4096).decode()
            print("Response:")
            print(response)
            
            expected_accept = base64.b64encode(
                hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
            ).decode()
            
            if "Sec-WebSocket-Accept: " + expected_accept in response:
                print("Handshake Success!")
            else:
                print("Handshake Failed! Expected: " + expected_accept)
        except Exception as e:
            print("Error: " + str(e))

if __name__ == "__main__":
    test_handshake()