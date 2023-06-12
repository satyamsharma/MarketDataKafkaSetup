#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <SQLiteCpp/SQLiteCpp.h>
#include <rdkafkacpp.h>
#include <thread>
#include <chrono>
#include <mutex>

std::mutex db_mutex;

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *userp)
{
   ((std::string*)userp)->append((char*)contents, size * nmemb);
   return size * nmemb;
}

std::string FetchData(const std::string &api_key, const std::string &symbol)
{
   CURL* curl;
   CURLcode resp;
   std::string readBuffer;

   curl_global_init(CURL_GLOBAL_DEFAULT);
   curl = curl_easy_init();

   if (curl)
   {
      std::string url = "https://cloud.iexapis.com/stable/stock/" + symbol + "/quote?token=" + api_key;
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
      resp = curl_easy_perform(curl);

      if (resp != CURLE_OK)
         fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(resp));

      curl_easy_cleanup(curl);
   }

   curl_global_cleanup();
   return readBuffer;
}

void sendToKafka(RdKafka::Producer *producer, const std::string &topicName, const std::string &data)
{
   std::cout << "sendToKafka topicName[" << topicName << "] data[" << data << "]" << std::endl;
   RdKafka::ErrorCode resp =
      producer->produce(topicName,
         RdKafka::Topic::PARTITION_UA,
         RdKafka::Producer::RK_MSG_COPY,
         const_cast<char*> (data.c_str()), data.size(),
         nullptr, 0, 0, nullptr, nullptr);

   if (resp != RdKafka::ERR_NO_ERROR)
   {
      std::cerr << RdKafka::err2str(resp) << std::endl;
   }

   producer->flush(5000);
}

void FeedFetcher(SQLite::Database &db, const std::string &api_key, const std::string &symbol)
{
   while (true)
   {
      std::string data = FetchData(api_key, symbol);
      nlohmann::json j = nlohmann::json::parse(data);

      long long epoch_timestamp = j["latestUpdate"].get<long long>();
      double price = j["latestPrice"].get<double>();
      std::string symbol = j["symbol"];

      std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(epoch_timestamp / 1000);
      std::time_t tt = std::chrono::system_clock::to_time_t(tp);
      std::tm * tm = std::localtime(&tt);
      std::stringstream ss;
      ss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");

      {
         std::lock_guard<std::mutex> lock(db_mutex);
         SQLite::Statement insert_query(db, "INSERT OR REPLACE INTO stock (timestamp, price, symbol) VALUES (?, ?, ?)");
         insert_query.bind(1, ss.str());
         insert_query.bind(2, price);
         insert_query.bind(3, symbol);
         insert_query.exec();
      }// lock deleted

      std::this_thread::sleep_for(std::chrono::seconds(10));
   }
}

void FeedPusher(SQLite::Database &db, RdKafka::Producer *producer, const std::string &topicName, const std::string &symbol)
{
   std::string lastTimestampSent;
   {
      std::lock_guard<std::mutex> lock(db_mutex);
      SQLite::Statement query(db, "SELECT timestamp, symbol, price FROM stock WHERE symbol='" + symbol + "' ORDER BY timestamp");
      while (query.executeStep())
      {
         lastTimestampSent = query.getColumn(0).getString();
         std::string data = lastTimestampSent + "|" + query.getColumn(1).getString() + "|" + std::to_string(query.getColumn(2).getDouble());
         sendToKafka(producer, topicName, data);
      }
   }// lock deleted

   std::string currTimestampSent = lastTimestampSent;

   while (true)
   {
      std::this_thread::sleep_for(std::chrono::seconds(10));

      std::string data;
      {
         std::lock_guard<std::mutex > lock(db_mutex);
         SQLite::Statement query(db, "SELECT timestamp, symbol, price FROM stock WHERE symbol='" + symbol + "' ORDER BY timestamp DESC LIMIT 1");
         if (query.executeStep())
         {
            currTimestampSent = query.getColumn(0).getString();
            if (currTimestampSent != lastTimestampSent)
            {
               data = currTimestampSent + "|" + query.getColumn(1).getString() + "|" + std::to_string(query.getColumn(2).getDouble());
               lastTimestampSent = currTimestampSent;
            }
            else
            {
               data.clear();
            }
         }
      }// lock deleted

      if (!data.empty())
      {
         sendToKafka(producer, topicName, data);
      }
   }
}

int main()
{
  std::string api_key = "pk_e60af08babbc4d5cab2b7a602e31d60c";
  std::string symbol = "MSFT";
  std::string topicName = symbol;

  // sqlite file
  auto now = std::chrono::system_clock::now();
  std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
  std::tm* now_tm = std::localtime(&now_time_t);
  std::ostringstream oss;
  oss << std::put_time(now_tm, "%Y%m%d") << ".db3";
  std::string db_filename = oss.str();

  SQLite::Database db(db_filename, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  db.exec("CREATE TABLE IF NOT EXISTS stock (timestamp DATETIME, price REAL, symbol TEXT, UNIQUE (timestamp))");

  std::string _err;

  RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
  if (conf->set("bootstrap.servers", "localhost", _err) != RdKafka::Conf::CONF_OK)
  {
    std::cerr << _err << std::endl;
    exit(1);
  }

   RdKafka::Producer *producer = RdKafka::Producer::create(conf, _err);
   if (!producer)
   {
      std::cerr << _err << std::endl;
      exit(1);
   }

   std::thread fetcher_t(FeedFetcher, std::ref(db), api_key, symbol);
   std::thread producer_t(FeedPusher, std::ref(db), producer, topicName, symbol);

   producer_t.join();
   fetcher_t.join();

   delete producer;
   delete conf;

   return 0;
}