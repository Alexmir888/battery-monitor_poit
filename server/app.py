import time
import sqlite3
import csv
from flask import Flask, render_template, request, jsonify
from flask_socketio import SocketIO

app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app, cors_allowed_origins="*")

system_state = {"is_open": False, "is_monitoring": False}
command_queue = []

def init_db():
    conn = sqlite3.connect('measurements.db')
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS log
                 (id INTEGER PRIMARY KEY AUTOINCREMENT,
                 timestamp REAL, v1 REAL, v2 REAL, v3 REAL, v4 REAL, v5 REAL, v6 REAL)''')
    conn.commit()
    conn.close()

init_db()

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/history', methods=['GET'])
def get_history():
    """Returns history data with time filtering support"""
    start_time = request.args.get('start', type=float)
    end_time = request.args.get('end', type=float)

    conn = sqlite3.connect('measurements.db')
    c = conn.cursor()

    if start_time and end_time:
        # If date parameters are provided, select records for this period
        c.execute("SELECT timestamp, v1, v2, v3, v4, v5, v6 FROM log WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp ASC", (start_time, end_time))
        rows = c.fetchall()
    else:
        # Select the last 50 records by default
        c.execute("SELECT timestamp, v1, v2, v3, v4, v5, v6 FROM log ORDER BY timestamp DESC LIMIT 50")
        rows = c.fetchall()
        rows.reverse() # Reverse for correct rendering on the chart
    
    conn.close()
    
    history_data = []
    for row in rows:
        history_data.append({
            "time": row[0], "v1": row[1], "v2": row[2], "v3": row[3], "v4": row[4], "v5": row[5], "v6": row[6]
        })
    
    return jsonify(history_data)

@app.route('/api/data', methods=['POST'])
def receive_data():
    data = request.json
    cmd_for_esp = command_queue.pop(0) if command_queue else "none"
    
    if not system_state["is_open"]:
        return jsonify({"status": "closed", "command": "none"})

    if data and system_state["is_monitoring"]:
        current_time = time.time()
        v1, v2, v3 = data.get('v1', 0), data.get('v2', 0), data.get('v3', 0)
        v4, v5, v6 = data.get('v4', 0), data.get('v5', 0), data.get('v6', 0)

        socketio.emit('update_data', {'time': current_time, 'v1': v1, 'v2': v2, 'v3': v3, 'v4': v4, 'v5': v5, 'v6': v6})

        conn = sqlite3.connect('measurements.db')
        c = conn.cursor()
        c.execute("INSERT INTO log (timestamp, v1, v2, v3, v4, v5, v6) VALUES (?, ?, ?, ?, ?, ?, ?)",
                  (current_time, v1, v2, v3, v4, v5, v6))
        conn.commit()
        conn.close()

        with open('log.csv', 'a', newline='') as f:
            writer = csv.writer(f)
            writer.writerow([current_time, v1, v2, v3, v4, v5, v6])

    return jsonify({"status": "success", "command": cmd_for_esp})

@socketio.on('system_command')
def handle_system_command(data):
    action = data.get('action')
    if action == 'open': system_state["is_open"] = True
    elif action == 'close':
        system_state["is_open"] = False
        system_state["is_monitoring"] = False
    elif action == 'start' and system_state["is_open"]: system_state["is_monitoring"] = True
    elif action == 'stop': system_state["is_monitoring"] = False
    socketio.emit('state_changed', system_state)

@socketio.on('relay_command')
def handle_relay_command(data):
    if system_state["is_open"]:
        command_queue.append(str(data.get('relay')))

if __name__ == '__main__':
    socketio.run(app, host='0.0.0.0', port=5000, debug=True)