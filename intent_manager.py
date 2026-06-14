# intent_manager.py
from enum import Enum


class Intent(Enum):
    NORMAL_CHAT = "NORMAL_CHAT"
    SENSOR_QUESTION = "SENSOR_QUESTION"
    START_QUERY = "START_QUERY"
    REFRESH = "REFRESH"
    EXIT = "EXIT"


class IntentManager:
    def detect(self, user_input: str) -> Intent:
        text = user_input.lower().strip()

        if text in ["exit", "quit"]:
            return Intent.EXIT

        if text in ["refresh", "reload"]:
            return Intent.REFRESH

        start_query_keywords = [
            "query",
            "ask me",
            "question me",
            "start question",
            "start check",
            "check my condition",
            "check status",
            "health check",
            "condition check",
            "interview me",
        ]

        for keyword in start_query_keywords:
            if keyword in text:
                return Intent.START_QUERY

        sensor_keywords = [
            "sensor",
            "sleep",
            "breathing",
            "temperature",
            "humidity",
            "noise",
            "light",
            "bright",
            "status",
            "abnormal",
            "problem",
            "issue",
            "concern",
        ]

        for keyword in sensor_keywords:
            if keyword in text:
                return Intent.SENSOR_QUESTION

        return Intent.NORMAL_CHAT
