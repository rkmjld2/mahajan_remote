import streamlit as st
from groq import Groq
import os
import requests
import io
import wave

# ────────────────────────────────────────────────
# CONFIG - Use Streamlit secrets (recommended for cloud)
# ────────────────────────────────────────────────
GROQ_API_KEY = st.secrets.get("GROQ_API_KEY")
ESP_IP = st.secrets.get("ESP_IP", "192.168.1.10")  # fallback for local testing

if not GROQ_API_KEY or not GROQ_API_KEY.startswith("gsk_"):
    st.error("GROQ_API_KEY is missing or invalid → please add it in Streamlit Cloud → Settings → Secrets")
    st.stop()

groq_client = Groq(api_key=GROQ_API_KEY)

# ────────────────────────────────────────────────
# BROWSER TTS (works remotely on client side!)
# ────────────────────────────────────────────────
def speak_browser(text: str):
    if not text:
        return
    safe_text = text.replace('"', '\\"').replace("'", "\\'")
    js = f"""
    <script>
    if ('speechSynthesis' in window) {{
        const utterance = new SpeechSynthesisUtterance("{safe_text}");
        utterance.lang = 'en-US';
        utterance.volume = 1.0;
        utterance.rate = 1.0;
        utterance.pitch = 1.0;
        window.speechSynthesis.speak(utterance);
    }} else {{
        console.log("SpeechSynthesis not supported");
    }}
    </script>
    """
    st.components.v1.html(js, height=0)

# ────────────────────────────────────────────────
# HELPERS
# ────────────────────────────────────────────────
def send_command(url: str) -> tuple[bool, str]:
    try:
        r = requests.get(url, timeout=5)
        if r.status_code == 200:
            return True, r.text.strip()
        return False, f"HTTP {r.status_code}"
    except Exception as e:
        return False, f"Error: {str(e)}"

# Simple command parser using Groq LLM
def parse_command_with_groq(user_text: str) -> tuple[str | None, str]:
    prompt = f"""You are a home automation assistant controlling D1 and D2 on an ESP8266.
ESP base URL: http://{ESP_IP}

User command: "{user_text}"

Respond ONLY in this exact format (two lines):

ACTION: <full http url like http://192.168.1.10/d1/on or NONE>
SPEAK: <short sentence to say back, e.g. D1 is now on>

Examples:
User: turn on d1 → ACTION: http://{ESP_IP}/d1/on   SPEAK: D1 is now on
User: switch off D2 please → ACTION: http://{ESP_IP}/d2/off   SPEAK: D2 is now off
User: status → ACTION: NONE   SPEAK: Use the buttons to check status

Now decide:"""

    try:
        resp = groq_client.chat.completions.create(
            model="llama-3.1-8b-instant",  # fast & cheap – you can change to "llama-3.1-70b-versatile" for better accuracy
            messages=[{"role": "user", "content": prompt}],
            temperature=0.1,
            max_tokens=100
        )
        answer = resp.choices[0].message.content.strip()

        action = None
        speak_text = "Sorry, I didn't understand."

        for line in answer.splitlines():
            line = line.strip()
            if line.startswith("ACTION:"):
                action = line.split(":", 1)[1].strip()
            elif line.startswith("SPEAK:"):
                speak_text = line.split(":", 1)[1].strip()

        if action == "NONE":
            action = None

        return action, speak_text

    except Exception as e:
        return None, f"Groq error: {str(e)}"

# ────────────────────────────────────────────────
# UI
# ────────────────────────────────────────────────
st.set_page_config(page_title="ESP8266 D1/D2 Control", layout="wide")

st.title("ESP8266 D1 / D2 Remote Control")
st.caption(f"Target ESP: http://{ESP_IP}   |   Deployed on Streamlit Cloud")

if st.button("Refresh / Check connection", help="Test if ESP is reachable"):
    ok, msg = send_command(f"http://{ESP_IP}")
    if ok:
        st.success("ESP responds → connection looks good")
    else:
        st.error(f"Cannot reach ESP → {msg}")

st.markdown("---")

st.subheader("Manual Buttons (works reliably from anywhere)")

col1, col2, col3, col4 = st.columns(4)

with col1:
    if st.button("D1 ON", use_container_width=True, type="primary"):
        ok, msg = send_command(f"http://{ESP_IP}/d1/on")
        st.session_state.status = msg if ok else f"Error: {msg}"
        speak_browser(msg if ok else "Failed to turn D1 on")

with col2:
    if st.button("D1 OFF", use_container_width=True):
        ok, msg = send_command(f"http://{ESP_IP}/d1/off")
        st.session_state.status = msg if ok else f"Error: {msg}"
        speak_browser(msg if ok else "Failed to turn D1 off")

with col3:
    if st.button("D2 ON", use_container_width=True, type="primary"):
        ok, msg = send_command(f"http://{ESP_IP}/d2/on")
        st.session_state.status = msg if ok else f"Error: {msg}"
        speak_browser(msg if ok else "Failed to turn D2 on")

with col4:
    if st.button("D2 OFF", use_container_width=True):
        ok, msg = send_command(f"http://{ESP_IP}/d2/off")
        st.session_state.status = msg if ok else f"Error: {msg}"
        speak_browser(msg if ok else "Failed to turn D2 off")

st.markdown("---")

st.subheader("Voice / Text Command (voice is experimental on cloud)")

st.info("""
**Voice input note (important!)**  
`st.audio_input` microphone often does **NOT work reliably** on Streamlit Community Cloud  
(even when browser permission is granted).  
→ Buttons are the most reliable way.  
→ Text input fallback always works.  
→ Voice may work in some browsers/devices — test it.
""")

tab_voice, tab_text = st.tabs(["🎤 Voice", "⌨️ Text fallback"])

with tab_voice:
    audio_data = st.audio_input(
        "Speak your command (e.g. turn on D1, switch off D2)",
        sample_rate=16000
    )

    if audio_data:
        with st.spinner("Transcribing with Groq Whisper..."):
            try:
                # Convert bytes → WAV in memory
                wav_buffer = io.BytesIO()
                with wave.open(wav_buffer, 'wb') as wav:
                    wav.setnchannels(1)
                    wav.setsampwidth(2)
                    wav.setframerate(16000)
                    wav.writeframes(audio_data.getvalue())
                wav_buffer.seek(0)

                transcription = groq_client.audio.transcriptions.create(
                    file=("audio.wav", wav_buffer, "audio/wav"),
                    model="whisper-large-v3",
                    response_format="text",
                    temperature=0.0
                ).strip()

                st.write("**You said:**", transcription)

                if transcription:
                    action_url, speak_text = parse_command_with_groq(transcription)
                    if action_url:
                        ok, msg = send_command(action_url)
                        result = msg if ok else f"Failed: {msg}"
                        st.success(result)
                        speak_browser(speak_text or result)
                    else:
                        st.warning(speak_text)
                        speak_browser(speak_text)

            except Exception as e:
                st.error(f"Voice processing failed: {str(e)}")
                speak_browser("Sorry, voice processing failed.")

with tab_text:
    text_cmd = st.text_input("Type command here (e.g. turn d1 on)", key="text_input")
    if st.button("Send text command") and text_cmd.strip():
        action_url, speak_text = parse_command_with_groq(text_cmd.strip())
        if action_url:
            ok, msg = send_command(action_url)
            result = msg if ok else f"Failed: {msg}"
            st.success(result)
            speak_browser(speak_text or result)
        else:
            st.warning(speak_text)
            speak_browser(speak_text)

# Status area
if "status" in st.session_state:
    st.markdown("---")
    st.subheader("Last action result")
    st.code(st.session_state.status)