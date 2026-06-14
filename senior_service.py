# senior_service.py
import json
import requests
from enum import Enum
from typing import Any, Dict

from relay_client import RelayClient
from abnormal_detector import AbnormalDetector
from intent_manager import IntentManager, Intent
from question_manager import QuestionManager
from Chat.chat import generate_sensor_natural_language


OLLAMA_URL = "http://localhost:11434/api/generate"
DIALOG_MODEL = "llama3.2:1b"


class ServiceMode(Enum):
    CHAT_MODE = "CHAT_MODE"
    QUESTION_MODE = "QUESTION_MODE"


class SeniorService:
    def __init__(self):
        self.mode = ServiceMode.CHAT_MODE

        self.relay_client = RelayClient()
        self.abnormal_detector = AbnormalDetector()
        self.intent_manager = IntentManager()
        self.question_manager = QuestionManager(max_questions=5)

        self.sensor_data: Dict[str, Any] = {}
        self.sensor_summary: str = ""
        self.abnormal_status: Dict[str, Any] = {
            "has_abnormal": False,
            "issues": [],
        }

    def fetch_and_check(self):
        self.sensor_data = self.relay_client.fetch()
        self.sensor_summary = generate_sensor_natural_language(self.sensor_data)
        self.abnormal_status = self.abnormal_detector.check(self.sensor_data)

    def call_ollama(self, model: str, prompt: str, max_tokens: int = 180) -> str:
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
- If the user asks how to start query mode, say:
  "Type 'start query' or 'check my condition' to enter QUESTION_MODE."

SENSOR SUMMARY:
{self.sensor_summary}

ABNORMAL STATUS:
{json.dumps(self.abnormal_status, ensure_ascii=False)}

USER:
{user_input}

ASSISTANT IN ENGLISH:
"""
        return self.call_ollama(DIALOG_MODEL, prompt)

    def enter_question_mode(self):
        self.mode = ServiceMode.QUESTION_MODE
        self.question_manager.reset()

        question = self.question_manager.next_question(self.abnormal_status)

        print(f"[QUESTION_MODE {self.question_manager.step}/{self.question_manager.max_questions}]")
        print(f"System Question: {question}")

    def handle_question_mode(self, user_input: str):
        if user_input.lower() in ["stop", "cancel", "back"]:
            print("System: QUESTION_MODE cancelled. Returning to CHAT_MODE.\n")
            self.mode = ServiceMode.CHAT_MODE
            self.question_manager.reset()
            return

        self.question_manager.save_answer(user_input)

        if self.question_manager.is_finished():
            self.finish_question_mode()
            return

        question = self.question_manager.next_question(self.abnormal_status)

        print(f"\n[QUESTION_MODE {self.question_manager.step}/{self.question_manager.max_questions}]")
        print(f"System Question: {question}")

    def finish_question_mode(self):
        print("\nSystem: The condition check is complete.")
        print("Summary:")

        print("\nAbnormal sensor issues:")
        for issue in self.abnormal_status.get("issues", []):
            print(
                f"- {issue['device']} / {issue['category']} / "
                f"{issue['priority']}: {issue['message']}"
            )

        print("\nCaregiver answers:")
        for item in self.question_manager.answers:
            print(f"- Q: {item['question']}")
            print(f"  A: {item['answer']}")

        print("\nSystem: Returning to CHAT_MODE.\n")

        self.mode = ServiceMode.CHAT_MODE
        self.question_manager.reset()

    def handle_chat_mode(self, user_input: str):
        intent = self.intent_manager.detect(user_input)

        if intent == Intent.START_QUERY:
            print("System: Starting QUESTION_MODE based on current sensor data.\n")

            if not self.abnormal_status["has_abnormal"]:
                print("System: No abnormal sensor status detected, but I will still ask general check questions.\n")

            self.enter_question_mode()
            return

        if intent == Intent.REFRESH:
            print("System: Refreshing Relay Server data...")
            self.fetch_and_check()
            self.print_abnormal_status()
            return

        if intent == Intent.EXIT:
            raise KeyboardInterrupt

        answer = self.chat_response(user_input)
        print(f"\nAssistant: {answer}\n")

    def run(self):
        print("Fetching Relay Server data...")
        self.fetch_and_check()
        print("Relay Server data loaded.\n")

        self.print_abnormal_status()

        if self.abnormal_status["has_abnormal"]:
            print("System: Entering QUESTION_MODE first because abnormal data exists.\n")
            self.enter_question_mode()
        else:
            print("System: Starting CHAT_MODE.\n")

        while True:
            try:
                if self.mode == ServiceMode.CHAT_MODE:
                    user_input = input("[CHAT_MODE] User: ").strip()
                    self.handle_chat_mode(user_input)

                elif self.mode == ServiceMode.QUESTION_MODE:
                    user_input = input("[QUESTION_MODE] User Answer: ").strip()

                    if user_input.lower() in ["exit", "quit"]:
                        print("Service stopped.")
                        break

                    self.handle_question_mode(user_input)

            except KeyboardInterrupt:
                print("\nService stopped.")
                break


if __name__ == "__main__":
    service = SeniorService()
    service.run()
