# senior_service.py
import json
import re
import requests
from enum import Enum
from typing import Any, Dict, List, Optional

from Chat.chat import generate_sensor_natural_language


RELAY_URL = "http://relay.emtake.com/api/query"
OLLAMA_URL = "http://localhost:11434/api/generate"

ACCOUNT = "test10@test.com"
REPORT_TYPE = "LLMREPORT"
CMD = "ALL"

DIALOG_MODEL = "llama3.2:1b"


class ServiceMode(Enum):
    CHAT_MODE = "CHAT_MODE"
    QUESTION_MODE = "QUESTION_MODE"


class SeniorService:
    def __init__(self):
        self.mode = ServiceMode.CHAT_MODE

        self.sensor_data: Dict[str, Any] = {}
        self.sensor_summary: str = ""
        self.abnormal_status: Dict[str, Any] = {"has_abnormal": False, "issues": []}

        self.question_step = 0
        self.max_questions = 5
        self.answers: List[Dict[str, str]] = []
        self.current_question: Optional[str] = None
        self.asked_categories: List[str] = []

    def fetch_relay_data(self) -> Dict[str, Any]:
        payload = {
            "Type": REPORT_TYPE,
            "Account": ACCOUNT,
            "CMD": CMD,
        }

        response = requests.post(RELAY_URL, json=payload, timeout=10)
        response.raise_for_status()

        data = response.json()
        if isinstance(data, str):
            data = json.loads(data)

        self.sensor_data = data
        self.sensor_summary = generate_sensor_natural_language(data)
        self.abnormal_status = self.check_abnormal_status()
        return data

    def call_ollama(self, model: str, prompt: str, max_tokens: int = 160) -> str:
        payload = {
            "model": model,
            "prompt": prompt,
            "stream": False,
            "options": {
                "temperature": 0.1,
                "top_p": 0.8,
                "num_predict": max_tokens,
            },
        }

        response = requests.post(OLLAMA_URL, json=payload, timeout=60)
        response.raise_for_status()
        return response.json().get("response", "").strip()

    def parse_count(self, text: Any) -> int:
        if text is None:
            return 0
        match = re.search(r"\d+", str(text))
        return int(match.group()) if match else 0

    def is_missing_device_data(self, device_data: Dict[str, Any]) -> bool:
        sleep = device_data.get("SleepData", {})
        if sleep.get("status") == "MISSING_DATA":
            return True

        small_keys = ["Temp", "Breath", "dB", "IndoorTemp", "Humidity", "Bright"]
        all_zero = True

        for key in small_keys:
            value = device_data.get(key, {})
            if not isinstance(value, dict):
                continue

            for number in value.values():
                if number != 0:
                    all_zero = False
                    break

        return all_zero

    def check_abnormal_status(self) -> Dict[str, Any]:
        issues = []

        for device_name, device_data in self.sensor_data.items():
            if not device_name.startswith("TEST") or not isinstance(device_data, dict):
                continue

            if self.is_missing_device_data(device_data):
                issues.append({
                    "device": device_name,
                    "category": "MISSING_DATA",
                    "priority": "LOW",
                    "message": "Sensor or sleep data is missing.",
                })
                continue

            sleep = device_data.get("SleepData", {})
            wakeup_count = self.parse_count(sleep.get("day_wakeup"))

            if wakeup_count >= 10:
                issues.append({
                    "device": device_name,
                    "category": "SLEEP",
                    "priority": "MEDIUM",
                    "message": f"Wake-up count is high: {wakeup_count} times.",
                })

            breath = device_data.get("Breath", {})
            breath_min = breath.get("Min")
            breath_max = breath.get("Max")

            if breath_min is not None and breath_min < 8:
                issues.append({
                    "device": device_name,
                    "category": "BREATHING",
                    "priority": "HIGH",
                    "message": f"Breathing minimum is low: {breath_min}.",
                })

            if breath_max is not None and breath_max > 24:
                issues.append({
                    "device": device_name,
                    "category": "BREATHING",
                    "priority": "HIGH",
                    "message": f"Breathing maximum is high: {breath_max}.",
                })

            indoor = device_data.get("IndoorTemp", {})
            indoor_min = indoor.get("Min")
            indoor_max = indoor.get("Max")

            if indoor_min is not None and indoor_min < 18:
                issues.append({
                    "device": device_name,
                    "category": "ROOM_TEMPERATURE",
                    "priority": "MEDIUM",
                    "message": f"Room temperature is low: {indoor_min}°C.",
                })

            if indoor_max is not None and indoor_max > 28:
                issues.append({
                    "device": device_name,
                    "category": "ROOM_TEMPERATURE",
                    "priority": "MEDIUM",
                    "message": f"Room temperature is high: {indoor_max}°C.",
                })

            humidity = device_data.get("Humidity", {})
            humidity_min = humidity.get("Min")
            humidity_max = humidity.get("Max")

            if humidity_min is not None and humidity_min < 30:
                issues.append({
                    "device": device_name,
                    "category": "HUMIDITY",
                    "priority": "LOW",
                    "message": f"Humidity is low: {humidity_min}%.",
                })

            if humidity_max is not None and humidity_max > 70:
                issues.append({
                    "device": device_name,
                    "category": "HUMIDITY",
                    "priority": "LOW",
                    "message": f"Humidity is high: {humidity_max}%.",
                })

            noise = device_data.get("dB", {})
            noise_max = noise.get("Max")

            if noise_max is not None and noise_max >= 80:
                issues.append({
                    "device": device_name,
                    "category": "NOISE",
                    "priority": "MEDIUM",
                    "message": f"Noise level is high: {noise_max} dB.",
                })

        return {
            "has_abnormal": len(issues) > 0,
            "issues": issues,
        }

    def print_abnormal_status(self):
        if not self.abnormal_status["has_abnormal"]:
            print("System: No abnormal sensor status detected.")
            return

        print("System: Abnormal sensor status detected.")
        for issue in self.abnormal_status["issues"]:
            print(
                f"- {issue['device']} / {issue['category']} / "
                f"{issue['priority']}: {issue['message']}"
            )

    def detect_user_check_request(self, user_input: str) -> bool:
        keywords = [
            "check", "status", "condition", "problem", "issue",
            "normal", "abnormal", "health", "sleep", "breathing",
        ]
        text = user_input.lower()
        return any(keyword in text for keyword in keywords)

    def chat_response(self, user_input: str) -> str:
        prompt = f"""
You are a sensor-based dialog assistant.

LANGUAGE RULE:
- Always answer in English.
- Do not use Korean.
- Do not mix languages.

DATA RULE:
- Use only the given sensor summary.
- Do not guess.
- Do not invent missing values.
- Do not provide medical diagnosis.
- Answer shortly and naturally.

SENSOR SUMMARY:
{self.sensor_summary}

ABNORMAL STATUS:
{json.dumps(self.abnormal_status, ensure_ascii=False)}

USER:
{user_input}

ASSISTANT IN ENGLISH:
"""
        return self.call_ollama(DIALOG_MODEL, prompt, max_tokens=180)

    def fallback_question(self) -> str:
        question_plan = [
            ("BREATHING", "Did the baby seem to have trouble breathing today?"),
            ("SLEEP", "Did the baby wake up or cry more often than usual during sleep?"),
            ("ROOM_TEMPERATURE", "Did the room feel unusually cold or hot while the baby was sleeping?"),
            ("NOISE", "Was there any loud noise while the baby was sleeping?"),
            ("MISSING_DATA", "Was the sensor device properly worn or connected today?"),
            ("HUMIDITY", "Did the room feel too dry or too humid today?"),
        ]

        available_categories = [
            issue.get("category")
            for issue in self.abnormal_status.get("issues", [])
        ]

        for category, question in question_plan:
            if category in available_categories and category not in self.asked_categories:
                self.asked_categories.append(category)
                return question

        general_questions = [
            "Did you notice anything unusual about the baby's condition today?",
            "Did the baby cry, move, or wake more than usual today?",
            "Was there any change in the baby's feeding, comfort, or sleep routine today?",
            "Did the room environment seem uncomfortable during sleep?",
            "Is there anything else you noticed that may explain the sensor readings?",
        ]

        idx = min(
            max(self.question_step - 1, 0),
            len(general_questions) - 1,
        )
        return general_questions[idx]

    def generate_next_question(self) -> str:
        self.question_step += 1
        question = self.fallback_question()
        self.current_question = question
        return question

    def handle_chat_mode(self, user_input: str):
        answer = self.chat_response(user_input)
        print(f"\nAssistant: {answer}\n")

        if self.detect_user_check_request(user_input):
            self.abnormal_status = self.check_abnormal_status()

            if self.abnormal_status["has_abnormal"]:
                self.print_abnormal_status()
                print("System: Entering QUESTION_MODE.\n")
                self.enter_question_mode()
            else:
                print("System: No abnormal status found. Staying in CHAT_MODE.\n")

    def enter_question_mode(self):
        self.mode = ServiceMode.QUESTION_MODE
        self.question_step = 0
        self.answers = []
        self.current_question = None
        self.asked_categories = []

        question = self.generate_next_question()

        print(f"[QUESTION_MODE {self.question_step}/{self.max_questions}]")
        print(f"System Question: {question}")

    def handle_question_mode(self, user_input: str):
        if user_input.lower() in ["stop", "cancel", "back"]:
            print("System: QUESTION_MODE cancelled. Returning to CHAT_MODE.\n")
            self.reset_question_mode()
            return

        self.answers.append({
            "question": self.current_question or "",
            "answer": user_input,
        })

        if self.question_step >= self.max_questions:
            self.finish_question_mode()
            return

        question = self.generate_next_question()

        print(f"\n[QUESTION_MODE {self.question_step}/{self.max_questions}]")
        print(f"System Question: {question}")

    def finish_question_mode(self):
        prompt = f"""
Summarize the sensor check session in English.

RULES:
- Use only the sensor summary, abnormal status, and user answers.
- Do not diagnose.
- Do not invent medical conclusions.
- Keep it short.
- Mention the main abnormal sensor issue if available.

SENSOR SUMMARY:
{self.sensor_summary}

ABNORMAL STATUS:
{json.dumps(self.abnormal_status, ensure_ascii=False)}

USER ANSWERS:
{json.dumps(self.answers, ensure_ascii=False)}

SUMMARY IN ENGLISH:
"""
        summary = self.call_ollama(DIALOG_MODEL, prompt, max_tokens=180)

        print("\nSystem: The condition check is complete.")
        print(f"Summary: {summary}")
        print("System: Returning to CHAT_MODE.\n")

        self.reset_question_mode()

    def reset_question_mode(self):
        self.mode = ServiceMode.CHAT_MODE
        self.question_step = 0
        self.answers = []
        self.current_question = None
        self.asked_categories = []

    def run(self):
        print("Fetching Relay Server data...")
        self.fetch_relay_data()
        print("Relay Server data loaded.\n")

        self.print_abnormal_status()

        if self.abnormal_status["has_abnormal"]:
            print("System: Entering QUESTION_MODE first because abnormal data exists.\n")
            self.enter_question_mode()
        else:
            print("System: Starting CHAT_MODE.\n")

        while True:
            if self.mode == ServiceMode.CHAT_MODE:
                user_input = input("[CHAT_MODE] User: ").strip()
            else:
                user_input = input("[QUESTION_MODE] User Answer: ").strip()

            if user_input.lower() in ["exit", "quit"]:
                print("Service stopped.")
                break

            if user_input.lower() in ["refresh", "reload"]:
                print("System: Refreshing Relay Server data...")
                self.fetch_relay_data()
                self.print_abnormal_status()

                if self.abnormal_status["has_abnormal"]:
                    print("System: Entering QUESTION_MODE because abnormal data exists.\n")
                    self.enter_question_mode()
                else:
                    print("System: Staying in CHAT_MODE.\n")
                    self.reset_question_mode()

                continue

            if self.mode == ServiceMode.CHAT_MODE:
                self.handle_chat_mode(user_input)
            elif self.mode == ServiceMode.QUESTION_MODE:
                self.handle_question_mode(user_input)


if __name__ == "__main__":
    service = SeniorService()
    service.run()
