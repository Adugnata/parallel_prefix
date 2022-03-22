#include <iostream>
#include <functional>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>

using namespace std::chrono;

template<class T>
class serial_prefix {
public:
  serial_prefix(// constructor definition
      vector<T> v // input/output vector
      , function<T(T, T)> f // combine function
  );

  auto run_serial(); // serial computation

  vector<T> output; // output vector
  function<T(T, T)> combine; // combine function
};

template<class T>
class parallel_prefix {

public:

  parallel_prefix(// constructor definition
      vector<T> v // input/output vector
      , function<T(T, T)> f // combine function
      , int w // number of worker
  );

    auto algorithm(vector<T> &v // input/output vector
            ,
                   int begin // beginning of partitioned vector
            ,
                   int stop // ending of partitioned vector
            ,
                   int threads // number of workers
            ,
                   bool main_thread // parent worker
    );
  auto run_parallel();// create and run thread

  vector<T> output; // output vector
  vector<long int> sc;// stage counter
  function<T(T, T)> combine; // combiner function
  long int workers, size, log2,ms;// parallelism degree, vector size, tree depth
  mutex mutx; // mutual exclusion
  condition_variable cv; // condition variable
};

template<class T>
serial_prefix<T>::serial_prefix(vector<T> v, function<T(T, T)> f) {
  // constructor declaration
  output = v;
  combine = f;
}

template<class T>
auto serial_prefix<T>::run_serial() {
  for (int i = 0; i < output.size() - 1; ++i) output[i + 1] = combine(output[i + 1], output[i]);
}


template<class T>
parallel_prefix<T>::parallel_prefix(vector<T> v, function<T(T, T)> f, int w) {
  // constructor declaration
  output =v;
  combine = f;
  workers = w;
  size = output.size();
  log2 = ceil(log(size) / log(2));
  sc.assign(2 * log2 - 1,0);
  ms = floor(log(100 * (workers + 4)) / log(2));
  ms = (workers > 1) ? ((ms < log2) ? ms : log2) : 0;

}
template<class T>
auto parallel_prefix<T>::algorithm(vector<T> &v, int begin, int stop, int threads,bool main_thread) {
  // up sweep phase
  for (int d = 0; d < log2-ms; ++d) {
    long int j = ceil(static_cast<double >(begin + 1) / ((1<<(d + 1)))) * (1<<(d + 1)) - 1;
    for (; j < stop && j < size; j += (1<<( d + 1))) v[j] = combine(v[j - (1<< d)], v[j]);
    unique_lock<mutex> lk(mutx);
    sc[d]++;
    if (sc[d] == threads) cv.notify_all();
    else while (sc[d] != threads) cv.wait(lk);
  }
  if(main_thread){
      for (int d = log2 - ms; d < log2; ++d){
          long int j =ceil(static_cast<double >(1.0) / (1<<( d+1))) *(1<<(d + 1)) - 1;
          for (; j < stop && j < size; j += (1<<( d + 1))) v[j] = combine(v[j - (1<< d)], v[j]);}
      for(int d = log2 - 2; d > log2 - 2 - ms && d >= 0; --d) {
          long int i = ceil(static_cast<double >(1.0) /(1<<(d + 1))) * (1<< (d + 1)) + (1<< d) - 1;
          for (; i < stop && i < size; i += (1<< (d + 1))) v[i] = combine(v[i], v[i - (1<< d)]);}
  }
  // down sweep phase
  for (int d = log2 - 2-ms; d >= 0; --d) {
   long int i = ceil(max(static_cast<double>(begin + 1 - (1<< d)), 1.0) /(1<< (d + 1))) *
    (1<<( d + 1)) + (1<< d) - 1;
    for (; i < stop && i < size; i += (1<< (d + 1))) v[i] = combine(v[i], v[i - (1<< d)]);
    long int stage_index = 2 * (log2 - 1) - d;
    unique_lock<mutex> lk(mutx);
    sc[stage_index]++;
    if (sc[stage_index] == threads) cv.notify_all();
    else while (sc[stage_index] != threads) cv.wait(lk);
  }
}

template<class T>
auto parallel_prefix<T>::run_parallel() {
    vector<int> p;
    p.emplace_back(size + 1);
    for (int i = 1; i < workers; ++i)
        p.emplace_back(size + 1 - i * floor(size / workers));
    p.emplace_back(0);
    vector<thread> threads; // create threads
    threads.emplace_back(&parallel_prefix<T>::algorithm, this, ref(output), p[1],
                         p[0], p.size() - 1, true); // main
    for (int i = 1; i < p.size() - 1; ++i)
        threads.emplace_back(&parallel_prefix<T>::algorithm, this, ref(output),
                             p[i + 1], p[i], p.size() - 1,
                             false); // starting child threads
    for (auto &t : threads)
        t.join(); // joining threads
}

auto report_time(int size, high_resolution_clock::duration span) {
  cout << "\nsequential execution of size = " << size << " - took - " << duration_cast<milliseconds>(span).count()
       << " milliseconds" << endl;
}

auto report_time(int size, int worker, int trials, high_resolution_clock::duration span) {
  cout << "\nparallel execution of size = " << size << ", worker = " << worker << ", trial = " << trials
       << " - took - " << duration_cast<milliseconds>(span).count() / trials << " milliseconds" << endl;

}

auto input_vector(int size) {
  vector<long int> input(size, 1);
  return input;
}
auto input_output(int argc, char **argv) {
  if (atoi(argv[1]) == 1) {//ready for sequential computation
    auto print(0), size(1000000);
    if (argc > 2) size = atoi(argv[2]);
    if (argc > 3) print = atoi(argv[3]);
    auto *object = new serial_prefix<long int>(input_vector(size), [](int a, int b) { return a + b; });
    auto ts = high_resolution_clock::now();
    object->run_serial();
    auto span = high_resolution_clock::now() - ts;
    if (print == 1) { for (auto i: object->output) cout << i << ' '; }
    report_time(size, span);
  } else if (atoi(argv[1]) == 2) {//ready for parallel computation
    auto size(1000000), workers(1), trials(1), print(0);
    parallel_prefix<long int> *object;
    milliseconds total_span(0);
    if (argc > 2) size = atoi(argv[2]);
    if (argc > 3) workers = atoi(argv[3]);
    if (argc > 4) trials = atoi(argv[4]);
    if (argc > 5) print = atoi(argv[5]);
    for (int t = 1; t <= trials; ++t) {
      object = new parallel_prefix<long int>(input_vector(size), [](int a, int b) { return a + b; }, workers);
      auto ts = high_resolution_clock::now();
      object->run_parallel();
      auto te = high_resolution_clock::now() - ts;
      total_span += duration_cast<milliseconds>(te);
    }
    if (print == 1) { for (auto i: object->output) cout << i << ' '; }
    report_time(size, workers, trials, total_span);
  }
}

int main(int argc, char *argv[]) {
  input_output(argc, argv);
  return 0;
}