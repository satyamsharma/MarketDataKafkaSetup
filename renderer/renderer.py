from flask import Flask, render_template, Response
from kafka import KafkaConsumer
import json

app = Flask(__name__)

@app.route('/')
def index():
    return render_template('index.html')

def kafka_consumer():
    # Connect to Kafka server and pass the topic name to Kafka Consumer
    consumer = KafkaConsumer('MSFT', bootstrap_servers='localhost:9092')
    for msg in consumer:
        message = msg.value.decode('utf-8')
        timestamp, symbol, price = message.split('|')
        data_dict = {"timestamp": timestamp, "symbol": symbol, "price": price}
        yield 'data:{0}\n\n'.format(json.dumps(data_dict))

@app.route('/kafka')
def newEvent():
    return Response(kafka_consumer(), mimetype='text/event-stream')

if __name__ == '__main__':
    app.run(host='localhost', port=8000, debug=True)