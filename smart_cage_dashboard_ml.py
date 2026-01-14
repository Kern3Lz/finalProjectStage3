"""
🐔 Smart Cage - ML Dashboard with Real-time Inference
Samsung Innovation Campus - Phase 3

Dashboard ini:
- Menerima data sensor dari ESP32 via MQTT (DHT11 + MQ2)
- Melakukan ML prediction menggunakan trained model
- Mengirim hasil prediksi kembali ke ESP32
- Toggle monitoring: Suhu atau Gas
"""

import streamlit as st
import sklearn
import pandas as pd
import json
import time
import paho.mqtt.client as mqtt
from datetime import datetime
import joblib
import os
import warnings

# Suppress sklearn warnings
warnings.filterwarnings('ignore', category=sklearn.exceptions.InconsistentVersionWarning)
warnings.filterwarnings('ignore', message='X does not have valid feature names')

# ============================================================
# MQTT CONFIGURATION
# ============================================================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883

# DHT11 Topics
TOPIC_DATA = "final-project/Mahasiswa-Berpola-Pikir/smartcage/data"
TOPIC_PREDICTION = "final-project/Mahasiswa-Berpola-Pikir/smartcage/prediction"

# MQ2 Gas Topics
TOPIC_GAS_DATA = "final-project/Mahasiswa-Berpola-Pikir/smartcage/gas/data"
TOPIC_GAS_PREDICTION = "final-project/Mahasiswa-Berpola-Pikir/smartcage/gas/prediction"

# LDR Light Topics
TOPIC_LDR_DATA = "final-project/Mahasiswa-Berpola-Pikir/smartcage/ldr/data"
TOPIC_LDR_PREDICTION = "final-project/Mahasiswa-Berpola-Pikir/smartcage/ldr/prediction"
    
# ============================================================
# SESSION STATE INITIALIZATION
# ============================================================
if "connected" not in st.session_state:
    st.session_state.connected = False

if "monitoring_mode" not in st.session_state:
    st.session_state.monitoring_mode = "suhu"

# DHT11 States
if "logs" not in st.session_state:
    st.session_state.logs = []

if "last_data" not in st.session_state:
    st.session_state.last_data = None

if "stats" not in st.session_state:
    st.session_state.stats = {
        "total_messages": 0,
        "ideal_count": 0,
        "panas_count": 0,
        "dingin_count": 0
    }

if "model" not in st.session_state:
    st.session_state.model = None

if "model_loaded" not in st.session_state:
    st.session_state.model_loaded = False

if "current_age_category" not in st.session_state:
    st.session_state.current_age_category = "0-3"

if "current_model_path" not in st.session_state:
    st.session_state.current_model_path = "smart_cage_model_03.pkl"

# Gas States
if "gas_logs" not in st.session_state:
    st.session_state.gas_logs = []

if "gas_last_data" not in st.session_state:
    st.session_state.gas_last_data = None

if "gas_stats" not in st.session_state:
    st.session_state.gas_stats = {
        "total_messages": 0,
        "aman_count": 0,
        "waspada_count": 0,
        "bahaya_count": 0
    }

if "gas_model" not in st.session_state:
    st.session_state.gas_model = None

if "gas_model_loaded" not in st.session_state:
    st.session_state.gas_model_loaded = False

# LDR States
if "ldr_logs" not in st.session_state:
    st.session_state.ldr_logs = []

if "ldr_last_data" not in st.session_state:
    st.session_state.ldr_last_data = None

if "ldr_stats" not in st.session_state:
    st.session_state.ldr_stats = {
        "total_messages": 0,
        "terang_count": 0,
        "redup_count": 0,
        "gelap_count": 0
    }

if "ldr_model" not in st.session_state:
    st.session_state.ldr_model = None

if "ldr_model_loaded" not in st.session_state:
    st.session_state.ldr_model_loaded = False

# Admin Mode - Password Protected Control
ADMIN_PASSWORD = "smartcage"
ADMIN_SESSION_FILE = "admin_session.json"

# ============================================================
# ADMIN SESSION HELPER FUNCTIONS
# ============================================================
import random
import string

def generate_session_id():
    """Generate unique session ID"""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    random_str = ''.join(random.choices(string.ascii_lowercase + string.digits, k=6))
    return f"{timestamp}_{random_str}"

def write_admin_session(session_id):
    """Write admin session to file"""
    session_data = {
        "session_id": session_id,
        "login_time": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }
    with open(ADMIN_SESSION_FILE, "w") as f:
        json.dump(session_data, f)

def read_admin_session():
    """Read current admin session from file"""
    if os.path.exists(ADMIN_SESSION_FILE):
        try:
            with open(ADMIN_SESSION_FILE, "r") as f:
                return json.load(f)
        except:
            return None
    return None

def clear_admin_session():
    """Delete session file on logout"""
    if os.path.exists(ADMIN_SESSION_FILE):
        os.remove(ADMIN_SESSION_FILE)

def check_session_valid(current_session_id):
    """Check if current session is still valid"""
    session_data = read_admin_session()
    if session_data is None:
        return False
    return session_data.get("session_id") == current_session_id

# Session state for admin
if "admin_logged_in" not in st.session_state:
    st.session_state.admin_logged_in = False

if "admin_session_id" not in st.session_state:
    st.session_state.admin_session_id = None

if "mqtt" not in st.session_state:
    st.session_state.mqtt = None

# Age model mapping
AGE_MODEL_MAPPING = {
    "0-3": "smart_cage_model_03.pkl",
    "4-7": "smart_cage_model_47.pkl",
    "8-14": "smart_cage_model_814.pkl",
    "15-21": "smart_cage_model_1521.pkl",
    "22-30": "smart_cage_model_2230.pkl"
}

# ============================================================
# LOAD ML MODELS
# ============================================================
def load_dht_model(model_path=None):
    if model_path is None:
        model_path = st.session_state.current_model_path
    
    if os.path.exists(model_path):
        try:
            model = joblib.load(model_path)
            st.session_state.model = model
            st.session_state.model_loaded = True
            st.session_state.current_model_path = model_path
            return True, f"✅ DHT11 Model loaded"
        except Exception as e:
            return False, f"❌ Error: {e}"
    return False, f"⚠️ File not found: {model_path}"

def load_gas_model():
    model_path = "mq2_gas_model.pkl"
    if os.path.exists(model_path):
        try:
            model = joblib.load(model_path)
            st.session_state.gas_model = model
            st.session_state.gas_model_loaded = True
            return True, f"✅ Gas Model loaded"
        except Exception as e:
            return False, f"❌ Error: {e}"
    return False, f"⚠️ mq2_gas_model.pkl not found"

def load_ldr_model():
    model_path = "ldr_light_model.pkl"
    if os.path.exists(model_path):
        try:
            model = joblib.load(model_path)
            st.session_state.ldr_model = model
            st.session_state.ldr_model_loaded = True
            return True, f"✅ LDR Model loaded"
        except Exception as e:
            return False, f"❌ Error: {e}"
    return False, f"⚠️ ldr_light_model.pkl not found"

# ============================================================
# SHARED SETTINGS (Sync across all sessions)
# ============================================================
SETTINGS_FILE = "current_settings.json"

def save_shared_settings():
    """Save current settings to shared file"""
    settings = {
        "age_category": st.session_state.current_age_category,
        "model_path": st.session_state.current_model_path
    }
    with open(SETTINGS_FILE, "w") as f:
        json.dump(settings, f)

def load_shared_settings():
    """Load settings from shared file"""
    if os.path.exists(SETTINGS_FILE):
        try:
            with open(SETTINGS_FILE, "r") as f:
                return json.load(f)
        except:
            return None
    return None

def sync_settings_from_file():
    """Sync session state with shared settings file"""
    settings = load_shared_settings()
    if settings:
        new_age = settings.get("age_category")
        if new_age and new_age != st.session_state.current_age_category:
            st.session_state.current_age_category = new_age
            if new_age in AGE_MODEL_MAPPING:
                load_dht_model(AGE_MODEL_MAPPING[new_age])

def change_age_category(age_category):
    if age_category in AGE_MODEL_MAPPING:
        st.session_state.current_age_category = age_category
        result = load_dht_model(AGE_MODEL_MAPPING[age_category])
        # Save to shared file so other sessions can sync
        save_shared_settings()
        return result
    return False, "Invalid category"

# ============================================================
# MQTT CALLBACKS (API Version 2)
# ============================================================
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        st.session_state.connected = True
        client.subscribe(TOPIC_DATA)
        client.subscribe(TOPIC_GAS_DATA)
        client.subscribe(TOPIC_LDR_DATA)
        print(f"✅ Connected & subscribed to all topics")
    else:
        st.session_state.connected = False
        print(f"❌ Connection failed: {reason_code}")

def on_message(client, userdata, msg):
    global st
    try:
        topic = msg.topic
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        if topic == TOPIC_DATA:
            handle_dht_message(client, data, timestamp)
        elif topic == TOPIC_GAS_DATA:
            handle_gas_message(client, data, timestamp)
        elif topic == TOPIC_LDR_DATA:
            handle_ldr_message(client, data, timestamp)
            
    except Exception as e:
        print(f"❌ Error: {e}")

def handle_dht_message(client, data, timestamp):
    temp = float(data.get('temp', 0))
    humidity = float(data.get('humidity', 0))
    
    kondisi = "Unknown"
    confidence = 0.0
    
    if st.session_state.model_loaded and st.session_state.model is not None:
        try:
            X_input = [[temp, humidity]]
            prediction = st.session_state.model.predict(X_input)[0]
            kondisi = prediction
            
            if hasattr(st.session_state.model, 'predict_proba'):
                proba = st.session_state.model.predict_proba(X_input)[0]
                confidence = max(proba) * 100
            else:
                confidence = 100.0
            
            prediction_payload = json.dumps({
                "kondisi": kondisi,
                "confidence": round(confidence, 2),
                "timestamp": timestamp
            })
            client.publish(TOPIC_PREDICTION, prediction_payload)
            
        except Exception as e:
            kondisi = "Error"
    else:
        kondisi = "No Model"
    
    log_entry = {
        "timestamp": timestamp,
        "temperature": temp,
        "humidity": humidity,
        "kondisi": kondisi,
        "confidence": confidence
    }
    
    st.session_state.last_data = log_entry
    st.session_state.logs.append(log_entry)
    
    st.session_state.stats["total_messages"] += 1
    if kondisi == "Ideal":
        st.session_state.stats["ideal_count"] += 1
    elif kondisi == "Panas":
        st.session_state.stats["panas_count"] += 1
    elif kondisi == "Dingin":
        st.session_state.stats["dingin_count"] += 1

def handle_gas_message(client, data, timestamp):
    gas_detected = data.get('gas_detected', False)
    temp = data.get('temp', 0)
    
    kondisi = "Unknown"
    confidence = 0.0
    
    if st.session_state.gas_model_loaded and st.session_state.gas_model is not None:
        try:
            gas_int = 1 if gas_detected else 0
            X_input = [[gas_int, temp]]
            prediction = st.session_state.gas_model.predict(X_input)[0]
            kondisi = prediction
            
            if hasattr(st.session_state.gas_model, 'predict_proba'):
                proba = st.session_state.gas_model.predict_proba(X_input)[0]
                confidence = max(proba) * 100
            else:
                confidence = 100.0
            
            prediction_payload = json.dumps({
                "kondisi": kondisi,
                "confidence": round(confidence, 2),
                "timestamp": timestamp
            })
            client.publish(TOPIC_GAS_PREDICTION, prediction_payload)
            
        except Exception as e:
            kondisi = "Error"
    else:
        # Rule-based fallback
        if not gas_detected:
            kondisi = "Aman"
        elif gas_detected and temp > 55:
            kondisi = "Bahaya"
        else:
            kondisi = "Waspada"
        confidence = 100.0
        
        prediction_payload = json.dumps({
            "kondisi": kondisi,
            "confidence": 100.0,
            "timestamp": timestamp
        })
        client.publish(TOPIC_GAS_PREDICTION, prediction_payload)
    
    log_entry = {
        "timestamp": timestamp,
        "gas_detected": gas_detected,
        "temp": temp,
        "kondisi": kondisi,
        "confidence": confidence
    }
    
    st.session_state.gas_last_data = log_entry
    st.session_state.gas_logs.append(log_entry)
    
    st.session_state.gas_stats["total_messages"] += 1
    if kondisi == "Aman":
        st.session_state.gas_stats["aman_count"] += 1
    elif kondisi == "Waspada":
        st.session_state.gas_stats["waspada_count"] += 1
    elif kondisi == "Bahaya":
        st.session_state.gas_stats["bahaya_count"] += 1

def handle_ldr_message(client, data, timestamp):
    ldr_value = int(data.get('ldr_value', 0))
    
    kondisi = "Unknown"
    confidence = 0.0
    
    if st.session_state.ldr_model_loaded and st.session_state.ldr_model is not None:
        try:
            X_input = [[ldr_value]]
            prediction = st.session_state.ldr_model.predict(X_input)[0]
            kondisi = prediction
            
            if hasattr(st.session_state.ldr_model, 'predict_proba'):
                proba = st.session_state.ldr_model.predict_proba(X_input)[0]
                confidence = max(proba) * 100
            else:
                confidence = 100.0
            
            prediction_payload = json.dumps({
                "kondisi": kondisi,
                "confidence": round(confidence, 2),
                "timestamp": timestamp
            })
            client.publish(TOPIC_LDR_PREDICTION, prediction_payload)
            
        except Exception as e:
            kondisi = "Error"
    else:
        # Rule-based fallback (ESP32 12-bit ADC: 0-4095)
        if ldr_value <= 1365:
            kondisi = "Terang"
        elif ldr_value <= 2730:
            kondisi = "Redup"
        else:
            kondisi = "Gelap"
        confidence = 100.0
        
        prediction_payload = json.dumps({
            "kondisi": kondisi,
            "confidence": 100.0,
            "timestamp": timestamp
        })
        client.publish(TOPIC_LDR_PREDICTION, prediction_payload)
    
    log_entry = {
        "timestamp": timestamp,
        "ldr_value": ldr_value,
        "kondisi": kondisi,
        "confidence": confidence
    }
    
    st.session_state.ldr_last_data = log_entry
    st.session_state.ldr_logs.append(log_entry)
    
    st.session_state.ldr_stats["total_messages"] += 1
    if kondisi == "Terang":
        st.session_state.ldr_stats["terang_count"] += 1
    elif kondisi == "Redup":
        st.session_state.ldr_stats["redup_count"] += 1
    elif kondisi == "Gelap":
        st.session_state.ldr_stats["gelap_count"] += 1

# ============================================================
# START MQTT CLIENT
# ============================================================
if st.session_state.mqtt is None:
    client = mqtt.Client(
        client_id=f"MLDashboard_{int(time.time())}",
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2
    )
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        st.session_state.mqtt = client
    except Exception as e:
        print(f"❌ Connection error: {e}")

# ============================================================
# STREAMLIT PAGE CONFIG
# ============================================================
st.set_page_config(
    page_title="Smart Cage ML Dashboard",
    page_icon="🤖",
    layout="wide"
)

# ============================================================
# SIDEBAR
# ============================================================
with st.sidebar:
    st.title("🤖 ML Dashboard")
    
    # ===== SYNC SETTINGS FROM FILE (for all sessions) =====
    sync_settings_from_file()
    
    # ===== CHECK SESSION VALIDITY =====
    if st.session_state.admin_logged_in:
        if not check_session_valid(st.session_state.admin_session_id):
            # Session taken over by another device
            st.session_state.admin_logged_in = False
            st.session_state.admin_session_id = None
            st.warning("⚠️ Session dipindah ke device lain!")
    
    # ===== ADMIN LOGIN SECTION =====
    st.subheader("🔐 Admin Access")
    if st.session_state.admin_logged_in:
        st.success("✅ Admin Mode Active")
        if st.button("🚪 Logout", width='stretch'):
            clear_admin_session()
            st.session_state.admin_logged_in = False
            st.session_state.admin_session_id = None
            st.rerun()
    else:
        st.info("👁️ Viewer Mode (Read-Only)")
        with st.expander("🔑 Login Admin"):
            password = st.text_input("Password", type="password", key="admin_pass")
            if st.button("Login", width='stretch'):
                if password == ADMIN_PASSWORD:
                    # Generate and store session
                    new_session_id = generate_session_id()
                    write_admin_session(new_session_id)
                    st.session_state.admin_session_id = new_session_id
                    st.session_state.admin_logged_in = True
                    st.success("Login berhasil!")
                    st.rerun()
                else:
                    st.error("Password salah!")
    
    st.divider()
    
    # MONITORING MODE TOGGLE (Available for all)
    st.subheader("📊 Mode Monitoring")
    col1, col2, col3 = st.columns(3)
    
    with col1:
        if st.button("🌡️ Suhu", width='stretch',
                     type="primary" if st.session_state.monitoring_mode == "suhu" else "secondary"):
            st.session_state.monitoring_mode = "suhu"
            st.rerun()
    
    with col2:
        if st.button("💨 Gas", width='stretch',
                     type="primary" if st.session_state.monitoring_mode == "gas" else "secondary"):
            st.session_state.monitoring_mode = "gas"
            st.rerun()
    
    with col3:
        if st.button("💡 LDR", width='stretch',
                     type="primary" if st.session_state.monitoring_mode == "ldr" else "secondary"):
            st.session_state.monitoring_mode = "ldr"
            st.rerun()
    
    st.info(f"Mode: **{st.session_state.monitoring_mode.upper()}**")
    st.divider()
    
    # MODE-SPECIFIC SETTINGS (ADMIN ONLY)
    if st.session_state.monitoring_mode == "suhu":
        st.subheader("🐔 Umur Ayam")
        
        if st.session_state.admin_logged_in:
            c1, c2 = st.columns(2)
            with c1:
                if st.button("🐣 0-3", width='stretch',
                             type="primary" if st.session_state.current_age_category == "0-3" else "secondary"):
                    change_age_category("0-3")
                    st.rerun()
                if st.button("🐥 8-14", width='stretch',
                             type="primary" if st.session_state.current_age_category == "8-14" else "secondary"):
                    change_age_category("8-14")
                    st.rerun()
                if st.button("🐓 22-30", width='stretch',
                             type="primary" if st.session_state.current_age_category == "22-30" else "secondary"):
                    change_age_category("22-30")
                    st.rerun()
            with c2:
                if st.button("🐤 4-7", width='stretch',
                             type="primary" if st.session_state.current_age_category == "4-7" else "secondary"):
                    change_age_category("4-7")
                    st.rerun()
                if st.button("🐔 15-21", width='stretch',
                             type="primary" if st.session_state.current_age_category == "15-21" else "secondary"):
                    change_age_category("15-21")
                    st.rerun()
        else:
            st.warning("🔒 Login admin untuk ubah umur")
        
        st.caption(f"Aktif: {st.session_state.current_age_category} hari")
        
        if not st.session_state.model_loaded:
            load_dht_model()
        
        if st.session_state.model_loaded:
            st.success("✅ Model loaded")
        else:
            st.warning("⚠️ No model")
            
    elif st.session_state.monitoring_mode == "gas":
        st.subheader("💨 Gas Model")
        
        if not st.session_state.gas_model_loaded:
            load_gas_model()
        
        if st.session_state.gas_model_loaded:
            st.success("✅ Gas model loaded")
        else:
            st.info("ℹ️ Using rule-based")
    
    elif st.session_state.monitoring_mode == "ldr":
        st.subheader("💡 LDR Model")
        
        if not st.session_state.ldr_model_loaded:
            load_ldr_model()
        
        if st.session_state.ldr_model_loaded:
            st.success("✅ LDR model loaded")
        else:
            st.info("ℹ️ Using rule-based")
    
    st.divider()
    
    # MQTT Status
    st.subheader("📡 MQTT")
    if st.session_state.connected:
        st.success("🟢 Connected")
    else:
        st.error("🔴 Disconnected")

# ============================================================
# MAIN DASHBOARD
# ============================================================
st.title("🐔 Smart Cage - ML Dashboard")
st.markdown("**Samsung Innovation Campus** | Mode: **" + st.session_state.monitoring_mode.upper() + "**")

# ============================================================
# SUHU MODE
# ============================================================
if st.session_state.monitoring_mode == "suhu":
    col1, col2, col3, col4 = st.columns(4)
    with col1:
        st.metric("Messages", st.session_state.stats["total_messages"])
    with col2:
        if st.session_state.last_data:
            st.metric("Temp", f"{st.session_state.last_data['temperature']:.1f}°C")
        else:
            st.metric("Temp", "---")
    with col3:
        if st.session_state.last_data:
            st.metric("Humidity", f"{st.session_state.last_data['humidity']:.1f}%")
        else:
            st.metric("Humidity", "---")
    with col4:
        if st.session_state.last_data:
            st.metric("Confidence", f"{st.session_state.last_data.get('confidence', 0):.1f}%")
        else:
            st.metric("Confidence", "---")
    
    st.divider()
    
    left, right = st.columns([1, 2])
    
    with left:
        st.subheader("🤖 Prediction")
        if st.session_state.last_data:
            kondisi = st.session_state.last_data["kondisi"]
            conf = st.session_state.last_data.get("confidence", 0)
            if kondisi == "Ideal":
                st.success(f"✅ {kondisi} ({conf:.1f}%)")
            elif kondisi == "Panas":
                st.error(f"🔥 {kondisi} ({conf:.1f}%)")
            elif kondisi == "Dingin":
                st.info(f"❄️ {kondisi} ({conf:.1f}%)")
            else:
                st.warning(f"❓ {kondisi}")
        else:
            st.info("⏳ Waiting...")
        
        st.divider()
        st.subheader("📈 Stats")
        stats = st.session_state.stats
        if stats["total_messages"] > 0:
            total = stats["total_messages"]
            st.write(f"✅ Ideal: {stats['ideal_count']} ({stats['ideal_count']/total*100:.1f}%)")
            st.write(f"🔥 Panas: {stats['panas_count']} ({stats['panas_count']/total*100:.1f}%)")
            st.write(f"❄️ Dingin: {stats['dingin_count']} ({stats['dingin_count']/total*100:.1f}%)")
    
    with right:
        st.subheader("📊 Monitoring - Suhu")
        if len(st.session_state.logs) >= 2:
            df = pd.DataFrame(st.session_state.logs)
            tab1, tab2, tab3 = st.tabs(["📈 Chart", "📊 Distribution", "📋 Data"])
            with tab1:
                chart_df = df[["timestamp", "temperature", "humidity"]].set_index("timestamp")
                st.line_chart(chart_df)
            with tab2:
                st.bar_chart(df["kondisi"].value_counts())
            with tab3:
                st.dataframe(df.tail(15), width='stretch')
        else:
            st.info("📡 Waiting for data...")

# ============================================================
# GAS MODE
# ============================================================
elif st.session_state.monitoring_mode == "gas":
    col1, col2, col3, col4 = st.columns(4)
    with col1:
        st.metric("Messages", st.session_state.gas_stats["total_messages"])
    with col2:
        if st.session_state.gas_last_data:
            st.metric("Gas", "YES" if st.session_state.gas_last_data['gas_detected'] else "NO")
        else:
            st.metric("Gas", "---")
    with col3:
        if st.session_state.gas_last_data:
            st.metric("Temp", f"{st.session_state.gas_last_data['temp']:.1f}°C")
        else:
            st.metric("Temp", "---")
    with col4:
        if st.session_state.gas_last_data:
            st.metric("Confidence", f"{st.session_state.gas_last_data.get('confidence', 0):.1f}%")
        else:
            st.metric("Confidence", "---")
    
    st.divider()
    
    left, right = st.columns([1, 2])
    
    with left:
        st.subheader("💨 Prediction")
        if st.session_state.gas_last_data:
            kondisi = st.session_state.gas_last_data["kondisi"]
            conf = st.session_state.gas_last_data.get("confidence", 0)
            if kondisi == "Aman":
                st.success(f"✅ {kondisi} ({conf:.1f}%)")
            elif kondisi == "Waspada":
                st.warning(f"⚠️ {kondisi} ({conf:.1f}%)")
            elif kondisi == "Bahaya":
                st.error(f"🚨 {kondisi} ({conf:.1f}%)")
            else:
                st.info(f"❓ {kondisi}")
        else:
            st.info("⏳ Waiting...")
        
        st.divider()
        st.subheader("📈 Stats")
        stats = st.session_state.gas_stats
        if stats["total_messages"] > 0:
            total = stats["total_messages"]
            st.write(f"✅ Aman: {stats['aman_count']} ({stats['aman_count']/total*100:.1f}%)")
            st.write(f"⚠️ Waspada: {stats['waspada_count']} ({stats['waspada_count']/total*100:.1f}%)")
            st.write(f"🚨 Bahaya: {stats['bahaya_count']} ({stats['bahaya_count']/total*100:.1f}%)")
    
    with right:
        st.subheader("📊 Monitoring - Gas")
        if len(st.session_state.gas_logs) >= 2:
            df = pd.DataFrame(st.session_state.gas_logs)
            tab1, tab2, tab3 = st.tabs(["📈 Chart", "📊 Distribution", "📋 Data"])
            with tab1:
                df["gas_int"] = df["gas_detected"].astype(int)
                chart_df = df[["timestamp", "gas_int", "temp"]].set_index("timestamp")
                st.line_chart(chart_df)
            with tab2:
                st.bar_chart(df["kondisi"].value_counts())
            with tab3:
                st.dataframe(df.tail(15), width='stretch')
        else:
            st.info("📡 Waiting for gas data...")

# ============================================================
# LDR MODE
# ============================================================
elif st.session_state.monitoring_mode == "ldr":
    col1, col2, col3, col4 = st.columns(4)
    with col1:
        st.metric("Messages", st.session_state.ldr_stats["total_messages"])
    with col2:
        if st.session_state.ldr_last_data:
            st.metric("LDR Value", st.session_state.ldr_last_data['ldr_value'])
        else:
            st.metric("LDR Value", "---")
    with col3:
        if st.session_state.ldr_last_data:
            kondisi = st.session_state.ldr_last_data['kondisi']
            st.metric("Kondisi", kondisi)
        else:
            st.metric("Kondisi", "---")
    with col4:
        if st.session_state.ldr_last_data:
            st.metric("Confidence", f"{st.session_state.ldr_last_data.get('confidence', 0):.1f}%")
        else:
            st.metric("Confidence", "---")
    
    st.divider()
    
    left, right = st.columns([1, 2])
    
    with left:
        st.subheader("💡 Prediction")
        if st.session_state.ldr_last_data:
            kondisi = st.session_state.ldr_last_data["kondisi"]
            conf = st.session_state.ldr_last_data.get("confidence", 0)
            if kondisi == "Terang":
                st.success(f"☀️ {kondisi} ({conf:.1f}%)")
            elif kondisi == "Redup":
                st.warning(f"🌤️ {kondisi} ({conf:.1f}%)")
            elif kondisi == "Gelap":
                st.error(f"🌙 {kondisi} ({conf:.1f}%)")
            else:
                st.info(f"❓ {kondisi}")
        else:
            st.info("⏳ Waiting...")
        
        st.divider()
        st.subheader("📈 Stats")
        stats = st.session_state.ldr_stats
        if stats["total_messages"] > 0:
            total = stats["total_messages"]
            st.write(f"☀️ Terang: {stats['terang_count']} ({stats['terang_count']/total*100:.1f}%)")
            st.write(f"🌤️ Redup: {stats['redup_count']} ({stats['redup_count']/total*100:.1f}%)")
            st.write(f"🌙 Gelap: {stats['gelap_count']} ({stats['gelap_count']/total*100:.1f}%)")
    
    with right:
        st.subheader("📊 Monitoring - LDR")
        if len(st.session_state.ldr_logs) >= 2:
            df = pd.DataFrame(st.session_state.ldr_logs)
            tab1, tab2, tab3 = st.tabs(["📈 Chart", "📊 Distribution", "📋 Data"])
            with tab1:
                chart_df = df[["timestamp", "ldr_value"]].set_index("timestamp")
                st.line_chart(chart_df)
            with tab2:
                st.bar_chart(df["kondisi"].value_counts())
            with tab3:
                st.dataframe(df.tail(15), width='stretch')
        else:
            st.info("📡 Waiting for LDR data...")

# ============================================================
# FOOTER
# ============================================================
st.divider()
c1, c2, c3 = st.columns(3)
with c1:
    st.caption(f"Mode: {st.session_state.monitoring_mode.upper()}")
with c2:
    if st.session_state.monitoring_mode == "suhu":
        st.caption(f"Data: {len(st.session_state.logs)}")
    else:
        st.caption(f"Data: {len(st.session_state.gas_logs)}")
with c3:
    if st.session_state.monitoring_mode == "suhu" and st.session_state.last_data:
        st.caption(f"Last: {st.session_state.last_data['timestamp']}")
    elif st.session_state.gas_last_data:
        st.caption(f"Last: {st.session_state.gas_last_data['timestamp']}")

# ============================================================
# MQTT LOOP & AUTO REFRESH
# ============================================================
if st.session_state.mqtt:
    st.session_state.mqtt.loop(timeout=0.1)

time.sleep(0.15)
st.rerun()
