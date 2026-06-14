# relay_client.py
import json
import requests


class RelayClient:
    def __init__(
        self,
        url="http://relay.emtake.com/api/query",
        account="test10@test.com",
        report_type="LLMREPORT",
        cmd="ALL",
    ):
        self.url = url
        self.account = account
        self.report_type = report_type
        self.cmd = cmd

    def fetch(self):
        payload = {
            "Type": self.report_type,
            "Account": self.account,
            "CMD": self.cmd,
        }

        response = requests.post(self.url, json=payload, timeout=10)
        response.raise_for_status()

        data = response.json()

        if isinstance(data, str):
            data = json.loads(data)

        return data
