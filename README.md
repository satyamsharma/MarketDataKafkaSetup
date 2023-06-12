# MarketData Kafka Setup

This is a concept setup that retrieves market data and serves the price chart on web-browser. It has two main components.

## `Dataloader` (C++)

- Fetches market data using IEX API
- Stores the retreived data in a sqlite database
- Streams the database contents into a Kafka topic
- Fetching and Streaming are done using independent threads with synchronized database accesses

## `Renderer` (Python/Flask, Javascript)

- Consumes the market data from the Kafka topic using a Python client
- Routes the data to the Javascript that uses Chart.js to build a real-time chart

## Setting up

### Kafka
- Make sure Kafka server is installed and running on the machine. https://www.digitalocean.com/community/tutorials/how-to-install-apache-kafka-on-ubuntu-20-04

### `Dataloader`
```sh
cd MarketDataKafkaSetup

# Download third-party dependencies
git submodule init
git submodule update

# May, also need to download additional system libraries using a package manager. Update CMakeLists.txt accordingly with the appropriate paths

mkdir build
cmake ..
make

# Make sure Kafka service is active
./market_data_loader
```

### `Renderer`
```
python3 -m venv env
source env/bin/activate

pip install -r requirements.txt

python3 renderer.py
```