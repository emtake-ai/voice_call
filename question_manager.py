# question_manager.py
from typing import Any, Dict, List, Optional


class QuestionManager:
    def __init__(self, max_questions: int = 5):
        self.max_questions = max_questions
        self.step = 0
        self.answers: List[Dict[str, str]] = []
        self.current_question: Optional[str] = None
        self.asked_categories: List[str] = []

    def reset(self):
        self.step = 0
        self.answers = []
        self.current_question = None
        self.asked_categories = []

    def format_value(self, value: Any, unit: str) -> str:
        if value is None:
            return "missing"

        if unit in ["°C", "%"]:
            return f"{value}{unit}"

        if unit:
            return f"{value} {unit}"

        return str(value)

    def build_question_from_issue(self, issue: Dict[str, Any]) -> str:
        category = issue.get("category", "")
        metric = issue.get("metric", "sensor value")
        value = self.format_value(issue.get("value"), issue.get("unit", ""))

        if category == "BREATHING":
            return (
                f"Your {metric} is {value}. "
                f"Did the baby seem to have difficulty breathing today?"
            )

        if category == "SLEEP":
            return (
                f"Your {metric} is {value}. "
                f"Did the baby wake up or cry more often than usual during sleep?"
            )

        if category == "ROOM_TEMPERATURE":
            return (
                f"Your {metric} is {value}. "
                f"Did the room feel unusually cold or hot while the baby was sleeping?"
            )

        if category == "NOISE":
            return (
                f"Your {metric} is {value}. "
                f"Was there any loud noise while the baby was sleeping?"
            )

        if category == "MISSING_DATA":
            return (
                "Some sensor or sleep data is missing. "
                "Was the sensor device properly worn or connected today?"
            )

        if category == "HUMIDITY":
            return (
                f"Your {metric} is {value}. "
                f"Did the room feel too dry or too humid today?"
            )

        return (
            f"Your {metric} is {value}. "
            f"Did you notice anything unusual today?"
        )

    def next_question(self, abnormal_status: Dict[str, Any]) -> str:
        self.step += 1

        priority_order = {
            "HIGH": 0,
            "MEDIUM": 1,
            "LOW": 2,
        }

        issue_order = [
            "BREATHING",
            "SLEEP",
            "ROOM_TEMPERATURE",
            "NOISE",
            "MISSING_DATA",
            "HUMIDITY",
        ]

        issues = abnormal_status.get("issues", [])

        sorted_issues = sorted(
            issues,
            key=lambda issue: (
                priority_order.get(issue.get("priority", "LOW"), 2),
                issue_order.index(issue.get("category"))
                if issue.get("category") in issue_order
                else 999,
            ),
        )

        for issue in sorted_issues:
            category = issue.get("category")
            if category not in self.asked_categories:
                self.asked_categories.append(category)
                self.current_question = self.build_question_from_issue(issue)
                return self.current_question

        general_questions = [
            "Did you notice anything unusual about the baby's condition today?",
            "Did the baby cry, move, or wake more than usual today?",
            "Was there any change in the baby's feeding, comfort, or sleep routine today?",
            "Did the room environment seem uncomfortable during sleep?",
            "Is there anything else you noticed that may explain the sensor readings?",
        ]

        idx = min(max(self.step - 1, 0), len(general_questions) - 1)
        self.current_question = general_questions[idx]
        return self.current_question

    def save_answer(self, answer: str):
        self.answers.append({
            "question": self.current_question or "",
            "answer": answer,
        })

    def is_finished(self) -> bool:
        return self.step >= self.max_questions
