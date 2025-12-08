"""
🐔 Smart Cage - ML Dashboard with Real-time Inference
Samsung Innovation Campus - Phase 3

Dashboard ini:
- Menerima data sensor dari ESP32 via MQTT
- Melakukan ML prediction menggunakan trained model
- Mengirim hasil prediksi kembali ke ESP32
- ESP32 men-trigger output berdasarkan prediksi ML
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

# ============================================================
# MQTT CONFIGURATION
# ============================================================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_DATA = "final-project/Mahasiswa-Berpola-Pikir/smartcage/data"
TOPIC_PREDICTION = "final-project/Mahasiswa-Berpola-Pikir/smartcage/prediction"
    
# ============================================================
# SESSION STATE INITIALIZATION
# ============================================================
if "connected" not in st.session_state:
    st.session_state.connected = False

if "logs" not in st.session_state:
    st.session_state.logs = []

if "last_data" not in st.session_state:
    st.session_state.last_data = None

if "mqtt" not in st.session_state:
    st.session_state.mqtt = None

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

if "model_metrics" not in st.session_state:
    st.session_state.model_metrics = None

if "current_age_category" not in st.session_state:
    st.session_state.current_age_category = "0-3"

if "current_model_path" not in st.session_state:
    st.session_state.current_model_path = "smart_cage_model_03.pkl"

# Age category to model file mapping
AGE_MODEL_MAPPING = {
    "0-3": "smart_cage_model_03.pkl",
    "4-7": "smart_cage_model_47.pkl",
    "8-14": "smart_cage_model_814.pkl",
    "15-21": "smart_cage_model_1521.pkl",
    "22-30": "smart_cage_model_2230.pkl"
}

# ============================================================
# LOAD ML MODEL
# ============================================================
def load_model(model_path=None):
    """Load trained ML model based on age category or custom path"""
    if model_path is None:
        model_path = st.session_state.current_model_path
    
    if os.path.exists(model_path):
        try:
            model = joblib.load(model_path)
            st.session_state.model = model
            st.session_state.model_loaded = True
            st.session_state.current_model_path = model_path
            return True, f"✅ Model loaded: {type(model).__name__} ({model_path})"
        except Exception as e:
            return False, f"❌ Error loading model: {e}"
    else:
        return False, f"⚠️ Model file not found: {model_path}"

def change_age_category(age_category):
    """Change the current age category and load corresponding model"""
    if age_category in AGE_MODEL_MAPPING:
        st.session_state.current_age_category = age_category
        model_path = AGE_MODEL_MAPPING[age_category]
        return load_model(model_path)
    return False, f"❌ Invalid age category: {age_category}"

# ============================================================
# MQTT CALLBACKS
# ============================================================
def on_connect(client, userdata, flags, rc, properties=None):
    """Callback when connected to MQTT broker"""
    if rc == 0:
        st.session_state.connected = True
        client.subscribe(TOPIC_DATA)
        print(f"✅ Connected to MQTT broker: {MQTT_BROKER}")
        print(f"📍 Subscribed to: {TOPIC_DATA}")
    else:
        st.session_state.connected = False
        print(f"❌ MQTT connection failed with code {rc}")


def on_message(client, userdata, msg):
    """Callback when receiving data from ESP32"""
    global st
    try:
        # Parse JSON from ESP32
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        
        # Extract data
        temp = float(data.get('temp', 0))
        humidity = float(data.get('humidity', 0))
        
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        # ML Prediction
        kondisi = "Unknown"
        confidence = 0.0
        
        if st.session_state.model_loaded and st.session_state.model is not None:
            try:
                # Prepare input
                X_input = [[temp, humidity]]
                
                # Predict
                prediction = st.session_state.model.predict(X_input)[0]
                kondisi = prediction
                
                # Get probability if available
                if hasattr(st.session_state.model, 'predict_proba'):
                    proba = st.session_state.model.predict_proba(X_input)[0]
                    confidence = max(proba) * 100
                else:
                    confidence = 100.0  # Default for models without proba
                
                # Publish prediction back to ESP32
                prediction_payload = json.dumps({
                    "kondisi": kondisi,
                    "confidence": round(confidence, 2),
                    "timestamp": timestamp
                })
                client.publish(TOPIC_PREDICTION, prediction_payload)
                
                print(f"📤 Published prediction: {kondisi} ({confidence:.1f}%)")
                
            except Exception as e:
                print(f"❌ Prediction error: {e}")
                kondisi = "Error"
        else:
            kondisi = "No Model"
        
        # Create log entry
        log_entry = {
            "timestamp": timestamp,
            "temperature": temp,
            "humidity": humidity,
            "kondisi": kondisi,
            "confidence": confidence
        }
        
        # Update session state
        st.session_state.last_data = log_entry
        st.session_state.logs.append(log_entry)
        
        # Update statistics
        st.session_state.stats["total_messages"] += 1
        if kondisi == "Ideal":
            st.session_state.stats["ideal_count"] += 1
        elif kondisi == "Panas":
            st.session_state.stats["panas_count"] += 1
        elif kondisi == "Dingin":
            st.session_state.stats["dingin_count"] += 1
        
        print(f"[{timestamp}] T={temp:.1f}°C | H={humidity:.1f}% | ML: {kondisi} ({confidence:.1f}%)")
        
    except Exception as e:
        print(f"❌ Error parsing message: {e}")


# ============================================================
# START MQTT CLIENT
# ============================================================
if st.session_state.mqtt is None:
    client = mqtt.Client(client_id=f"MLDashboard_{int(time.time())}")
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        st.session_state.mqtt = client
        print(f"🔌 Connecting to {MQTT_BROKER}...")
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
# SIDEBAR - Model Management
# ============================================================
with st.sidebar:
    st.title("🤖 ML Model")
    
    # Age Category Selection
    st.subheader("🐔 Pilih Umur Ayam")
    st.caption("Klik button untuk memilih model sesuai umur ayam")
    
    # Create 2 columns for buttons
    col_btn1, col_btn2 = st.columns(2)
    
    with col_btn1:
        if st.button("🐣 0-3 Hari", use_container_width=True, 
                     type="primary" if st.session_state.current_age_category == "0-3" else "secondary"):
            success, message = change_age_category("0-3")
            if success:
                st.toast(f"✅ Model umur 0-3 hari berhasil dimuat!")
            else:
                st.toast(f"❌ Gagal memuat model: {message}")
            st.rerun()
        
        if st.button("🐥 8-14 Hari", use_container_width=True,
                     type="primary" if st.session_state.current_age_category == "8-14" else "secondary"):
            success, message = change_age_category("8-14")
            if success:
                st.toast(f"✅ Model umur 8-14 hari berhasil dimuat!")
            else:
                st.toast(f"❌ Gagal memuat model: {message}")
            st.rerun()
        
        if st.button("🐓 22-30 Hari", use_container_width=True,
                     type="primary" if st.session_state.current_age_category == "22-30" else "secondary"):
            success, message = change_age_category("22-30")
            if success:
                st.toast(f"✅ Model umur 22-30 hari berhasil dimuat!")
            else:
                st.toast(f"❌ Gagal memuat model: {message}")
            st.rerun()
    
    with col_btn2:
        if st.button("🐤 4-7 Hari", use_container_width=True,
                     type="primary" if st.session_state.current_age_category == "4-7" else "secondary"):
            success, message = change_age_category("4-7")
            if success:
                st.toast(f"✅ Model umur 4-7 hari berhasil dimuat!")
            else:
                st.toast(f"❌ Gagal memuat model: {message}")
            st.rerun()
        
        if st.button("🐔 15-21 Hari", use_container_width=True,
                     type="primary" if st.session_state.current_age_category == "15-21" else "secondary"):
            success, message = change_age_category("15-21")
            if success:
                st.toast(f"✅ Model umur 15-21 hari berhasil dimuat!")
            else:
                st.toast(f"❌ Gagal memuat model: {message}")
            st.rerun()
    
    # Show current selection
    st.info(f"📌 **Aktif:** Umur {st.session_state.current_age_category} hari")
    
    st.divider()
    
    # Model upload
    st.subheader("📤 Upload Trained Model")
    uploaded_file = st.file_uploader("Upload .pkl model file", type=['pkl'])
    
    if uploaded_file is not None:
        # Save with current age category name
        model_filename = st.session_state.current_model_path
        with open(model_filename, "wb") as f:
            f.write(uploaded_file.getbuffer())
        st.success(f"Model file uploaded as {model_filename}!")
        
        # Reload model
        success, message = load_model()
        if success:
            st.success(message)
        else:
            st.error(message)
    
    # Try to load model if exists
    if not st.session_state.model_loaded:
        success, message = load_model()
        if success:
            st.info(message)
        else:
            st.warning(message)
    
    st.divider()
    
    # Model status
    st.subheader("📊 Model Status")
    if st.session_state.model_loaded:
        st.success("✅ Model Loaded")
        st.write(f"**Type:** {type(st.session_state.model).__name__}")
        st.write(f"**File:** {st.session_state.current_model_path}")
        st.write(f"**Umur:** {st.session_state.current_age_category} hari")
    else:
        st.error("❌ No Model Loaded")
        st.info("Pilih kategori umur ayam atau upload model (.pkl)")
    
    st.divider()
    
    # MQTT Status
    st.subheader("📡 MQTT Status")
    if st.session_state.connected:
        st.success("🟢 Connected")
    else:
        st.error("🔴 Disconnected")
    
    st.write(f"**Broker:** {MQTT_BROKER}")
    st.write(f"**Topic Data:** `{TOPIC_DATA}`")
    st.write(f"**Topic Prediction:** `{TOPIC_PREDICTION}`")


# ============================================================
# MAIN DASHBOARD
# ============================================================
st.title("🐔 Smart Cage - ML Dashboard")
st.markdown("**Samsung Innovation Campus - Phase 3**")
st.markdown("Dashboard with **Machine Learning Inference** | Real-time prediction from trained model")

# Header metrics
col1, col2, col3, col4 = st.columns(4)

with col1:
    st.metric("Total Messages", st.session_state.stats["total_messages"])

with col2:
    if st.session_state.last_data:
        temp_value = st.session_state.last_data["temperature"]
        st.metric("Current Temp", f"{temp_value:.1f}°C")
    else:
        st.metric("Current Temp", "---")

with col3:
    if st.session_state.last_data:
        humidity_value = st.session_state.last_data["humidity"]
        st.metric("Current Humidity", f"{humidity_value:.1f}%")
    else:
        st.metric("Current Humidity", "---")

with col4:
    if st.session_state.last_data and st.session_state.model_loaded:
        confidence = st.session_state.last_data.get("confidence", 0)
        st.metric("ML Confidence", f"{confidence:.1f}%")
    else:
        st.metric("ML Confidence", "---")

st.divider()

# Main layout
left_panel, right_panel = st.columns([1, 2])

# ============================================================
# LEFT PANEL
# ============================================================
with left_panel:
    st.subheader("🤖 Latest ML Prediction")
    
    if st.session_state.last_data and st.session_state.model_loaded:
        data = st.session_state.last_data
        kondisi = data["kondisi"]
        confidence = data.get("confidence", 0)
        
        # Display with color
        if kondisi == "Ideal":
            st.success(f"✅ **{kondisi}** ({confidence:.1f}% confidence)")
        elif kondisi == "Panas":
            st.error(f"🔥 **{kondisi}** ({confidence:.1f}% confidence)")
        elif kondisi == "Dingin":
            st.info(f"❄️ **{kondisi}** ({confidence:.1f}% confidence)")
        else:
            st.warning(f"❓ **{kondisi}**")
        
        st.write(f"**Time:** {data['timestamp']}")
        st.write(f"**Temp:** {data['temperature']:.1f}°C")
        st.write(f"**Humidity:** {data['humidity']:.1f}%")
        
    elif not st.session_state.model_loaded:
        st.warning("⚠️ No model loaded. Upload a trained model in the sidebar.")
    else:
        st.info("⏳ Waiting for sensor data...")
    
    st.divider()
    
    st.subheader("📈 Prediction Statistics")
    stats = st.session_state.stats
    
    total = stats["total_messages"]
    if total > 0:
        ideal_pct = (stats["ideal_count"] / total) * 100
        panas_pct = (stats["panas_count"] / total) * 100
        dingin_pct = (stats["dingin_count"] / total) * 100
        
        st.write(f"**Total Predictions:** {total}")
        st.write(f"✅ **Ideal:** {stats['ideal_count']} ({ideal_pct:.1f}%)")
        st.write(f"🔥 **Panas:** {stats['panas_count']} ({panas_pct:.1f}%)")
        st.write(f"❄️ **Dingin:** {stats['dingin_count']} ({dingin_pct:.1f}%)")
        
        # Health score
        st.divider()
        st.subheader("🏆 Health Score")
        
        if ideal_pct >= 80:
            st.success(f"⭐⭐⭐ EXCELLENT")
        elif ideal_pct >= 60:
            st.info(f"⭐⭐ GOOD")
        elif ideal_pct >= 40:
            st.warning(f"⭐ FAIR")
        else:
            st.error(f"⚠️ NEEDS ATTENTION")
        
        st.progress(ideal_pct / 100)
        st.caption(f"Ideal conditions: {ideal_pct:.1f}%")
    else:
        st.info("No predictions yet")
    
    st.divider()
    
    st.subheader("💾 Download Data")
    if len(st.session_state.logs) > 0:
        df = pd.DataFrame(st.session_state.logs)
        csv = df.to_csv(index=False).encode('utf-8')
        
        st.download_button(
            label="📥 Download CSV",
            data=csv,
            file_name=f"ml_predictions_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv",
            mime="text/csv"
        )
        st.caption(f"Total rows: {len(df)}")
    else:
        st.info("No data to download")


# ============================================================
# RIGHT PANEL - Visualizations
# ============================================================
with right_panel:
    st.subheader("📊 Real-time Monitoring")
    
    if len(st.session_state.logs) >= 2:
        df = pd.DataFrame(st.session_state.logs)
        
        tab1, tab2, tab3 = st.tabs(["📈 Time Series", "📊 Distribution", "📋 Data Table"])
        
        with tab1:
            st.markdown("### Temperature & Humidity Over Time")
            
            # Combined chart
            chart_df = df[["timestamp", "temperature", "humidity"]].copy()
            chart_df = chart_df.set_index("timestamp")
            st.line_chart(chart_df)
            
        with tab2:
            st.markdown("### ML Prediction Distribution")
            
            # Bar chart
            kondisi_counts = df["kondisi"].value_counts()
            st.bar_chart(kondisi_counts)
            
            # Percentage breakdown
            st.markdown("### Percentage Breakdown")
            col_a, col_b, col_c = st.columns(3)
            
            total_pred = len(df)
            with col_a:
                ideal_pct = (stats["ideal_count"] / total_pred * 100) if total_pred > 0 else 0
                st.metric("✅ Ideal", f"{ideal_pct:.1f}%")
            
            with col_b:
                panas_pct = (stats["panas_count"] / total_pred * 100) if total_pred > 0 else 0
                st.metric("🔥 Panas", f"{panas_pct:.1f}%")
            
            with col_c:
                dingin_pct = (stats["dingin_count"] / total_pred * 100) if total_pred > 0 else 0
                st.metric("❄️ Dingin", f"{dingin_pct:.1f}%")
        
        with tab3:
            st.markdown("### ML Prediction Log (Latest 20)")
            display_df = df[["timestamp", "temperature", "humidity", "kondisi", "confidence"]].tail(20)
            st.dataframe(display_df, use_container_width=True)
            
            st.markdown("### Statistical Summary")
            st.dataframe(df[["temperature", "humidity", "confidence"]].describe(), use_container_width=True)
    
    else:
        st.info("📡 Waiting for data from ESP32...\n\nMake sure:\n1. ESP32 is running and connected to WiFi\n2. ESP32 is publishing to the correct MQTT topic\n3. ML model is uploaded in the sidebar")


# ============================================================
# FOOTER
# ============================================================
st.divider()
footer_col1, footer_col2, footer_col3 = st.columns(3)

with footer_col1:
    ml_status = "🤖 ML Active" if st.session_state.model_loaded else "⚠️ No Model"
    st.caption(f"{ml_status}")

with footer_col2:
    st.caption(f"📊 Predictions: {len(st.session_state.logs)}")

with footer_col3:
    if st.session_state.last_data:
        last_time = st.session_state.last_data["timestamp"]
        st.caption(f"🕐 Last: {last_time}")
    else:
        st.caption("🕐 Last: Never")


# ============================================================
# MQTT LOOP & AUTO REFRESH
# ============================================================
if st.session_state.mqtt:
    st.session_state.mqtt.loop(timeout=0.1)

# Auto refresh every 1 second
time.sleep(1)
st.rerun()
