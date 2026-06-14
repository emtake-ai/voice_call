# voice_call

Sensor-based voice/chat service for senior or baby-care monitoring.

## Project Structure

```text
voice_call/
├── senior_service.py
├── relay_client.py
├── abnormal_detector.py
├── intent_manager.py
├── question_manager.py
├── Chat/
├── Fetch/
├── Plan/
├── Query/
└── README.md
```

## Main Flow

```text
User
  |
  v
senior_service.py
  |
  +--> relay_client.py
  |        |
  |        +--> Fetch sensor data
  |
  +--> Chat/chat.py
  |        |
  |        +--> Build sensor summary
  |
  +--> abnormal_detector.py
  |        |
  |        +--> Detect abnormal conditions
  |
  +--> intent_manager.py
  |        |
  |        +--> CHAT_MODE / QUESTION_MODE
  |
  +--> question_manager.py
           |
           +--> Follow-up questions
```

## Run

```bash
pip install requests
python3 senior_service.py
```

## Modes

### CHAT_MODE
User asks questions about current sensor status.

### QUESTION_MODE
System asks follow-up questions when abnormal conditions are detected.

## Ollama

```bash
ollama pull llama3.2:1b
ollama serve
```

Default endpoint:

```text
http://localhost:11434/api/generate
```
