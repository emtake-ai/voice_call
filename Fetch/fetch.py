# fetch.py
import json
import requests

URL = "http://relay.emtake.com/api/query"

def fetch_all(account="test10@test.com", val=None, date=None):
    payload = {
        "Type": "LLMREPORT",
        "Account": account,
        "CMD": "ALL"
    }

    # val 우선순위가 date보다 높음
    if val is not None:
        payload["val"] = str(val)
    elif date is not None:
        payload["date"] = date

    headers = {
        "Content-Type": "application/json"
    }

    response = requests.post(URL, headers=headers, json=payload, timeout=10)
    response.raise_for_status()

    data = response.json()

    # 응답이 문자열 JSON으로 올 경우 한 번 더 파싱
    if isinstance(data, str):
        data = json.loads(data)

    return data


if __name__ == "__main__":
    # 마지막 날 데이터
    result = fetch_all(account="test10@test.com")

    # 특정 날짜 데이터
    # result = fetch_all(account="test10@test.com", date="2026-05-05")

    # 마지막 날로부터 val일치 데이터, date보다 우선
    # result = fetch_all(account="test10@test.com", val=1)

    print(json.dumps(result, indent=2, ensure_ascii=False))
