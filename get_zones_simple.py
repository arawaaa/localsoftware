import requests
import urllib3
import json
import time

# Disable insecure request warnings for local HTTPS
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

HOST = "192.168.12.10"
CLIENT_ID = "simple_zone_requester"

def get_zones():
    try:
        # 1. Connect
        connect_url = f"https://{HOST}/Endpoints/{CLIENT_ID}/Connect"
        requests.post(connect_url, verify=False, timeout=5).raise_for_status()

        # 2. Request /zones data
        request_url = f"https://{HOST}/Messages/RequestData"
        payload = {
            "MessageId": "1",
            "MessageType": "RequestData",
            "SenderId": CLIENT_ID,
            "TargetId": "LCC",
            "AdditionalParameters": {"JSONPath": "1;/zones"}
        }
        requests.post(request_url, json=payload, verify=False, timeout=5).raise_for_status()

        # 3. Poll for the data (S40 can take up to 45s to respond with full zones)
        print("Polling for /zones data (this may take up to 60 seconds)...")
        retrieve_url = f"https://{HOST}/Messages/{CLIENT_ID}/Retrieve"
        
        for i in range(60):
            resp = requests.get(retrieve_url, verify=False, timeout=5)
            if resp.status_code == 200 and resp.text:
                data = resp.json()
                for msg in data.get('messages', []):
                    if "Data" in msg and "zones" in msg["Data"]:
                        full_zones = msg["Data"]["zones"]
                        if len(str(full_zones)) > 500: 
                            print(f"Data received on poll {i+1}.")
                            return full_zones
            time.sleep(1)
        
        return "Timed out: Zone data not received."

    except Exception as e:
        return f"Error: {str(e)}"
    finally:
        # 4. Disconnect
        try:
            disconnect_url = f"https://{HOST}/Endpoints/{CLIENT_ID}/Disconnect"
            requests.post(disconnect_url, verify=False, timeout=5)
        except:
            pass

if __name__ == "__main__":
    result = get_zones()
    if isinstance(result, list) or isinstance(result, dict):
        print("\n--- EXPANDED ZONES JSON ---")
        print(json.dumps(result, indent=2))
    else:
        print(result)