# senior_service.py
import json
import requests
from enum import Enum

from Chat.chat_english import generate_sensor_natural_language


RELAY_URL = "http://relay.emtake.com/api/query"
OLLAMA_URL = "http://localhost:11434/api/generate"

ACCOUNT = "test10@test.com"
REPORT_TYPE = "LLMREPORT"
CMD = "ALL"

DIALOG_MODEL = "llama3.2:1b"
QUERY_MODEL = "llama3.2:1b"


class ServiceMode(Enum):
    CHAT_MODE = "CHAT_MODE"
    QUESTION_MODE = "QUESTION_MODE"


class SeniorService:
    def __init__(self):
        self.mode = ServiceMode.CHAT_MODE

        self.sensor_data = {}
        self.sensor_summary = ""

        self.question_step = 0
        self.max_questions = 5
        self.answers = []
        self.current_question = None

    def fetch_relay_data(self):
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

        return data

    def call_ollama(self, model: str, prompt: str) -> str:
        payload = {
            "model": model,
            "prompt": prompt,
            "stream": False,
        }

        response = requests.post(OLLAMA_URL, json=payload, timeout=60)
        response.raise_for_status()

        return response.json().get("response", "").strip()

    def detect_need_question_mode(self, user_input: str) -> bool:
        keywords = [
            "check",
            "status",
            "problem",
            "normal",
            "abnormal",
            "issue",
            "condition",
            "health",
            "sleep",
            "breathing",
        ]

        text = user_input.lower()
        return any(keyword in text for keyword in keywords)

    def chat_response(self, user_input: str) -> str:
        prompt = f"""
You are a sensor-based dialog assistant.

Use only the given sensor summary.
Do not guess.
Do not invent missing values.
Answer shortly and naturally.
Answer in the same language as the user.

SENSOR SUMMARY:
{self.sensor_summary}

USER:
{user_input}

ASSISTANT:
"""
        return self.call_ollama(DIALOG_MODEL, prompt)

    def generate_next_question(self) -> str:
        self.question_step += 1

        prompt = f"""
You are a sensor follow-up question assistant.

Based on the sensor summary and previous user answers,
ask ONE useful follow-up question.

Rules:
- Ask only one question.
- Do not explain.
- Use the same language as the previous user answer if possible.
- Focus on sleep, breathing, body temperature, room temperature, humidity, noise, or light level.
- This is question {self.question_step} of {self.max_questions}.

SENSOR SUMMARY:
{self.sensor_summary}

PREVIOUS ANSWERS:
{json.dumps(self.answers, ensure_ascii=False)}

QUESTION:
"""
        question = self.call_ollama(QUERY_MODEL, prompt)
        self.current_question = question
        return question

    def handle_chat_mode(self, user_input: str):
        answer = self.chat_response(user_input)
        print(f"\nAssistant: {answer}\n")

        if self.detect_need_question_mode(user_input):
            print("System: I will ask a few follow-up questions to check the condition.\n")
            self.enter_question_mode()

    def enter_question_mode(self):
        self.mode = ServiceMode.QUESTION_MODE
        self.question_step = 0
        self.answers = []
        self.current_question = None

        question = self.generate_next_question()

        print(f"[QUESTION_MODE {self.question_step}/{self.max_questions}]")
        print(f"System Question: {question}")

    def handle_question_mode(self, user_input: str):
        self.answers.append(
            {
                "question": self.current_question,
                "answer": user_input,
            }
        )

        if self.question_step >= self.max_questions:
            self.finish_question_mode()
            return

        question = self.generate_next_question()

        print(f"\n[QUESTION_MODE {self.question_step}/{self.max_questions}]")
        print(f"System Question: {question}")

    def finish_question_mode(self):
        prompt = f"""
Summarize the sensor check session shortly.

Use only the given sensor summary and user answers.
Do not diagnose.
Do not invent medical conclusions.
Provide a simple condition summary.

SENSOR SUMMARY:
{self.sensor_summary}

USER ANSWERS:
{json.dumps(self.answers, ensure_ascii=False)}

SUMMARY:
"""
        summary = self.call_ollama(DIALOG_MODEL, prompt)

        print("\nSystem: The condition check is complete.")
        print(f"Summary: {summary}")
        print("System: Returning to CHAT_MODE.\n")

        self.mode = ServiceMode.CHAT_MODE
        self.question_step = 0
        self.answers = []
        self.current_question = None

    def run(self):
        print("Fetching Relay Server data...")
        self.fetch_relay_data()
        print("Relay Server data loaded.\n")

        while True:
            if self.mode == ServiceMode.CHAT_MODE:
                user_input = input("[CHAT_MODE] User: ").strip()
            else:
                user_input = input("[QUESTION_MODE] User Answer: ").strip()

            if user_input.lower() in ["exit", "quit"]:
                print("Service stopped.")
                break

            if user_input.lower() in ["refresh", "reload"]:
                self.fetch_relay_data()
                print("Relay Server data refreshed.\n")
                continue

            if self.mode == ServiceMode.CHAT_MODE:
                self.handle_chat_mode(user_input)
            elif self.mode == ServiceMode.QUESTION_MODE:
                self.handle_question_mode(user_input)


if __name__ == "__main__":
    service = SeniorService()
    service.run()
