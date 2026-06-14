# chat.py
import json


SMALL_SENSOR_LABELS = {
    "Temp": "체온",
    "Breath": "호흡",
    "IndoorTemp": "실내 온도",
    "Humidity": "습도",
    "dB": "소음",
    "Bright": "밝기",
}


def minutes_to_hm(minutes: int) -> str:
    h = minutes // 60
    m = minutes % 60
    return f"{h}시간 {m}분"


def summarize_small_sensors(device_data: dict) -> list[str]:
    lines = []

    temp = device_data.get("Temp", {})
    if temp:
        lines.append(f"체온 변화 값은 최소 {temp.get('Min')}에서 최대 {temp.get('Max')}입니다.")

    breath = device_data.get("Breath", {})
    if breath:
        lines.append(f"호흡 값은 최소 {breath.get('Min')}에서 최대 {breath.get('Max')}입니다.")

    indoor = device_data.get("IndoorTemp", {})
    if indoor:
        lines.append(f"실내 온도는 최소 {indoor.get('Min')}도에서 최대 {indoor.get('Max')}도입니다.")

    humidity = device_data.get("Humidity", {})
    if humidity:
        lines.append(f"습도는 최소 {humidity.get('Min')}%에서 최대 {humidity.get('Max')}%입니다.")

    db = device_data.get("dB", {})
    if db:
        lines.append(f"최대 소음은 {db.get('Max')}dB입니다.")

    bright = device_data.get("Bright", {})
    if bright:
        lines.append(f"방 밝기 값은 최소 {bright.get('Min')}에서 최대 {bright.get('Max')}입니다.")

    return lines


def summarize_sleep(sleep: dict) -> list[str]:
    lines = []

    status = sleep.get("status", "UNKNOWN")

    if status == "MISSING_DATA":
        return ["수면 데이터가 없습니다. 상태는 MISSING_DATA입니다."]

    day_gs = sleep.get("day_gs")
    day_pr = sleep.get("day_pr")
    day_wakeup = sleep.get("day_wakeup")
    week_gs = sleep.get("week_gs")
    month_gs = sleep.get("month_gs")

    lines.append(f"수면 상태는 {status}입니다.")

    if day_gs:
        lines.append(f"오늘 좋은 수면 시간은 {day_gs}입니다.")
    if day_pr:
        lines.append(f"오늘 뒤척임 시간은 {day_pr}입니다.")
    if day_wakeup:
        lines.append(f"오늘 중간에 깬 횟수는 {day_wakeup}입니다.")
    if week_gs and month_gs:
        lines.append(f"주간 평균 좋은 수면은 {week_gs}, 월간 평균 좋은 수면은 {month_gs}입니다.")

    sessions = sleep.get("sessions", [])
    if sessions:
        lines.append(f"오늘 감지된 수면 구간은 총 {len(sessions)}개입니다.")

        for i, s in enumerate(sessions, start=1):
            start = s.get("start")
            end = s.get("end")
            duration = s.get("duration_min", 0)
            wake_up = s.get("wake_up", 0)

            lines.append(
                f"{i}번째 수면은 {start}부터 {end}까지이며, "
                f"{minutes_to_hm(duration)} 동안 지속되었고, "
                f"중간에 {wake_up}번 깼습니다."
            )

    return lines


def summarize_device(device_name: str, device_data: dict, baby_name: str) -> str:
    lines = []

    lines.append(f"[{device_name}] {baby_name}의 센서 요약입니다.")

    sleep = device_data.get("SleepData", {})
    lines.extend(summarize_sleep(sleep))

    lines.append("환경 및 생체 센서 요약입니다.")
    lines.extend(summarize_small_sensors(device_data))

    last_update = device_data.get("last_update_at")
    if last_update:
        lines.append(f"마지막 데이터 업데이트 시간은 {last_update}입니다.")

    return "\n".join(lines)


def generate_sensor_natural_language(data: dict) -> str:
    baby_name = data.get("name", "아이")
    gender = data.get("gender", "UNKNOWN")
    birth_date = data.get("birth_date", "UNKNOWN")

    result = []
    result.append(f"아이 이름은 {baby_name}이고, 성별은 {gender}, 생년월일은 {birth_date}입니다.")

    for key, value in data.items():
        if key.startswith("TEST") and isinstance(value, dict):
            result.append("")
            result.append(summarize_device(key, value, baby_name))

    return "\n".join(result)


if __name__ == "__main__":
    # fetch.py에서 받은 JSON을 여기에 넣으면 됨
    with open("relay_data.json", "r", encoding="utf-8") as f:
        relay_data = json.load(f)

    summary = generate_sensor_natural_language(relay_data)

    print(summary)
