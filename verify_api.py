import requests
import urllib3
import json
import time

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

host = "192.168.12.10"
client_id = "test_client"

def test_connection():
    try:
        print(f"Connecting to {host}...")
        url = f"https://{host}/Endpoints/{client_id}/Connect"
        resp = requests.post(url, verify=False, timeout=5)
        print(f"Connect status: {resp.status_code}")

        if resp.status_code in [200, 204]:
            print("Requesting data...")
            url = f"https://{host}/Messages/RequestData"
            body = {
                "MessageId": "1",
                "MessageType": "RequestData",
                "SenderId": client_id,
                "TargetId": "LCC",
                "AdditionalParameters": {
                    "JSONPath": "1;/devices;/equipments;/zones"
                }
            }
            resp = requests.post(url, json=body, verify=False, timeout=5)
            print(f"RequestData status: {resp.status_code}")

            print("Retrieving messages...")
            for _ in range(3):
                url = f"https://{host}/Messages/{client_id}/Retrieve"
                resp = requests.get(url, verify=False, timeout=5)
                print(f"Retrieve status: {resp.status_code}")
                if resp.text:
                    print(f"Retrieve response: {json.dumps(resp.json(), indent=2)}")
                else:
                    print("No messages yet, sleeping...")
                time.sleep(1)

            print("Disconnecting...")
            url = f"https://{host}/Endpoints/{client_id}/Disconnect"
            resp = requests.post(url, verify=False, timeout=5)
            print(f"Disconnect status: {resp.status_code}")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    test_connection()