# abnormal_detector.py
import re
from typing import Any, Dict


class AbnormalDetector:
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

    def check(self, sensor_data: Dict[str, Any]) -> Dict[str, Any]:
        issues = []

        for device_name, device_data in sensor_data.items():
            if not device_name.startswith("TEST") or not isinstance(device_data, dict):
                continue

            if self.is_missing_device_data(device_data):
                issues.append({
                    "device": device_name,
                    "category": "MISSING_DATA",
                    "priority": "LOW",
                    "metric": "data",
                    "value": None,
                    "unit": "",
                    "direction": "missing",
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
                    "metric": "wake-up count",
                    "value": wakeup_count,
                    "unit": "times",
                    "direction": "high",
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
                    "metric": "minimum breathing value",
                    "value": breath_min,
                    "unit": "bpm",
                    "direction": "low",
                    "message": f"Breathing minimum is low: {breath_min} bpm.",
                })

            if breath_max is not None and breath_max > 24:
                issues.append({
                    "device": device_name,
                    "category": "BREATHING",
                    "priority": "HIGH",
                    "metric": "maximum breathing value",
                    "value": breath_max,
                    "unit": "bpm",
                    "direction": "high",
                    "message": f"Breathing maximum is high: {breath_max} bpm.",
                })

            indoor = device_data.get("IndoorTemp", {})
            indoor_min = indoor.get("Min")
            indoor_max = indoor.get("Max")

            if indoor_min is not None and indoor_min < 18:
                issues.append({
                    "device": device_name,
                    "category": "ROOM_TEMPERATURE",
                    "priority": "MEDIUM",
                    "metric": "minimum room temperature",
                    "value": indoor_min,
                    "unit": "°C",
                    "direction": "low",
                    "message": f"Room temperature is low: {indoor_min}°C.",
                })

            if indoor_max is not None and indoor_max > 28:
                issues.append({
                    "device": device_name,
                    "category": "ROOM_TEMPERATURE",
                    "priority": "MEDIUM",
                    "metric": "maximum room temperature",
                    "value": indoor_max,
                    "unit": "°C",
                    "direction": "high",
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
                    "metric": "minimum humidity",
                    "value": humidity_min,
                    "unit": "%",
                    "direction": "low",
                    "message": f"Humidity is low: {humidity_min}%.",
                })

            if humidity_max is not None and humidity_max > 70:
                issues.append({
                    "device": device_name,
                    "category": "HUMIDITY",
                    "priority": "LOW",
                    "metric": "maximum humidity",
                    "value": humidity_max,
                    "unit": "%",
                    "direction": "high",
                    "message": f"Humidity is high: {humidity_max}%.",
                })

            noise = device_data.get("dB", {})
            noise_max = noise.get("Max")

            if noise_max is not None and noise_max >= 80:
                issues.append({
                    "device": device_name,
                    "category": "NOISE",
                    "priority": "MEDIUM",
                    "metric": "maximum noise level",
                    "value": noise_max,
                    "unit": "dB",
                    "direction": "high",
                    "message": f"Noise level is high: {noise_max} dB.",
                })

        return {
            "has_abnormal": len(issues) > 0,
            "issues": issues,
        }
