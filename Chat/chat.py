# Chat/chat.py
import json


def minutes_to_hm(minutes: int) -> str:
    hours = minutes // 60
    mins = minutes % 60
    return f"{hours}h {mins}m"


def summarize_small_sensors(device_data: dict) -> list[str]:
    lines = []

    temp = device_data.get("Temp", {})
    if temp:
        lines.append(f"Body temperature value ranged from {temp.get('Min')} to {temp.get('Max')}.")

    breath = device_data.get("Breath", {})
    if breath:
        lines.append(f"Breathing value ranged from {breath.get('Min')} to {breath.get('Max')}.")

    indoor = device_data.get("IndoorTemp", {})
    if indoor:
        lines.append(f"Room temperature ranged from {indoor.get('Min')}°C to {indoor.get('Max')}°C.")

    humidity = device_data.get("Humidity", {})
    if humidity:
        lines.append(f"Humidity ranged from {humidity.get('Min')}% to {humidity.get('Max')}%.")

    noise = device_data.get("dB", {})
    if noise:
        lines.append(f"The maximum noise level was {noise.get('Max')} dB.")

    bright = device_data.get("Bright", {})
    if bright:
        lines.append(f"The room light level ranged from {bright.get('Min')} to {bright.get('Max')}.")

    return lines


def summarize_sleep(sleep_data: dict) -> list[str]:
    lines = []

    status = sleep_data.get("status", "UNKNOWN")

    if status == "MISSING_DATA":
        return ["Sleep data is missing. The status is MISSING_DATA."]

    lines.append(f"The sleep status is {status}.")

    if sleep_data.get("day_gs"):
        lines.append(f"Today's good sleep time was {sleep_data.get('day_gs')}.")

    if sleep_data.get("day_pr"):
        lines.append(f"Today's restless period was {sleep_data.get('day_pr')}.")

    if sleep_data.get("day_wakeup"):
        lines.append(f"The baby woke up {sleep_data.get('day_wakeup')} today.")

    if sleep_data.get("week_gs"):
        lines.append(f"The weekly average good sleep time is {sleep_data.get('week_gs')}.")

    if sleep_data.get("month_gs"):
        lines.append(f"The monthly average good sleep time is {sleep_data.get('month_gs')}.")

    sessions = sleep_data.get("sessions", [])
    if sessions:
        lines.append(f"There were {len(sessions)} sleep sessions today.")

        for idx, session in enumerate(sessions, start=1):
            start = session.get("start")
            end = session.get("end")
            duration = session.get("duration_min", 0)
            wake_up = session.get("wake_up", 0)

            lines.append(
                f"Sleep session {idx}: from {start} to {end}, "
                f"lasting {minutes_to_hm(duration)}, "
                f"with {wake_up} wake-ups."
            )

    return lines


def summarize_device(device_name: str, device_data: dict, baby_name: str) -> str:
    lines = []

    lines.append(f"[{device_name}] Sensor summary for {baby_name}.")

    sleep_data = device_data.get("SleepData", {})
    lines.extend(summarize_sleep(sleep_data))

    lines.append("Small sensor summary:")
    lines.extend(summarize_small_sensors(device_data))

    last_update = device_data.get("last_update_at")
    if last_update:
        lines.append(f"The last update time was {last_update}.")

    return "\n".join(lines)


def generate_sensor_natural_language(data: dict) -> str:
    baby_name = data.get("name", "the baby")
    gender = data.get("gender", "UNKNOWN")
    birth_date = data.get("birth_date", "UNKNOWN")

    result = []
    result.append(
        f"The baby's name is {baby_name}, gender is {gender}, "
        f"and birth date is {birth_date}."
    )

    for key, value in data.items():
        if key.startswith("TEST") and isinstance(value, dict):
            result.append("")
            result.append(summarize_device(key, value, baby_name))

    return "\n".join(result)


if __name__ == "__main__":
    with open("relay_data.json", "r", encoding="utf-8") as f:
        relay_data = json.load(f)

    summary = generate_sensor_natural_language(relay_data)
    print(summary)
