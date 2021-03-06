#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include <cuckoomap/CuckooHelpers.h>
#include <cuckoomap/CuckooMap.h>

#define MIN(a, b) (((a) <= (b)) ? (a) : (b))

#define KEY_PAD 4
#define VALUE_PAD 4

struct Key {
  int k;
  static constexpr char padding[KEY_PAD] = {0};
  Key() : k(0) {}
  Key(int i) : k(i) {}
  bool empty() { return k == 0; }
};

struct Value {
  int v;
  static constexpr char padding[VALUE_PAD] = {0};
  Value() : v(0) {}
  Value(int i) : v(i) {}
  bool empty() { return v == 0; }
};

namespace std {
template <>
struct equal_to<Key> {
  bool operator()(Key const& a, Key const& b) const { return a.k == b.k; }
};
}

class RandomNumber {
 private:
  unsigned _current;

 public:
  RandomNumber(unsigned seed) : _current(seed) {}
  unsigned next() {
    // https://en.wikipedia.org/wiki/Linear_congruential_generator
    _current = (48271 * _current % 2147483647);
    return _current;
  }
  unsigned nextInRange(int range) {
    if (range == 0) {
      return 0;
    }
    next();
    return next() % range;
  }
};

class WeightedSelector {
 private:
  RandomNumber _r;
  std::vector<unsigned> _cutoffs;

 public:
  WeightedSelector(unsigned seed, std::vector<double> weights) : _r(seed) {
    double totalWeight = 0.0;
    for (unsigned i = 0; i < weights.size(); i++) {
      totalWeight += weights[i];
      _cutoffs.push_back(std::ceil(totalWeight * 2147483647.0));
    }
  }
  unsigned next() {
    unsigned sample = _r.next();
    unsigned i = 0;
    for (; i < _cutoffs.size(); i++) {
      if (sample < _cutoffs[i]) break;
    }
    return i;
  }
};

typedef HashWithSeed<Key, 0xdeadbeefdeadbeefULL> KeyHash;
typedef std::unordered_map<Key, Value*, KeyHash> unordered_map;

class TestMap {
 private:
  int _useCuckoo;
  std::unique_ptr<CuckooMap<Key, Value>> _cuckoo;
  std::unique_ptr<unordered_map> _unordered;

 public:
  TestMap(int useCuckoo, size_t initialSize) : _useCuckoo(useCuckoo) {
    if (_useCuckoo) {
      _cuckoo.reset(new CuckooMap<Key, Value>(initialSize));
    } else {
      _unordered.reset(new unordered_map(initialSize));
    }
  }
  Value* lookup(Key const& k) {
    if (_useCuckoo) {
      auto element = _cuckoo->lookup(k);
      return (element.found() ? element.value() : nullptr);
    } else {
      auto element = _unordered->find(k);
      return (element != _unordered->end()) ? (*element).second : nullptr;
    }
  }
  bool insert(Key const& k, Value* v) {
    if (_useCuckoo) {
      return _cuckoo->insert(k, v);
    } else {
      return _unordered->emplace(k, v).second;
    }
  }
  bool remove(Key const& k) {
    if (_useCuckoo) {
      return _cuckoo->remove(k);
    } else {
      return (_unordered->erase(k) > 0);
    }
  }
};

// Usage: PerformanceTest [cuckoo] [nInitial] [nTotal] [nWorking] [pInsert]
//          [pLookup] [pRemove] [pWorking] [pMiss] [seed]
//    [cuckoo]: 1 = use CuckooMap; 0 = Use std::unordered_map
//    [nOpCount]: Number of operations to run
//    [nInitialSize]: Initial number of elements
//    [nMaxSize]: Maximum number of elements
//    [nWorking]: Size of working set
//    [pInsert]: Probability of insert
//    [pLookup]: Probability of lookup
//    [pRemove]: Probability of remove
//    [pWorking]: Probability of operation staying in working set
//    [pMiss]: Probability of lookup for missing element
//    [seed]: Seed for PRNG
int main(int argc, char* argv[]) {
  if (argc < 12) {
    std::cerr << "Incorrect number of parameters." << std::endl;
    exit(-1);
  }

  unsigned useCuckoo = atoi(argv[1]);
  unsigned nOpCount = atoi(argv[2]);
  unsigned nInitialSize = atoi(argv[3]);
  unsigned nMaxSize = atoi(argv[4]);
  unsigned nWorking = atoi(argv[5]);
  double pInsert = atof(argv[6]);
  double pLookup = atof(argv[7]);
  double pRemove = atof(argv[8]);
  double pWorking = atof(argv[9]);
  double pMiss = atof(argv[10]);
  unsigned seed = atoi(argv[11]);

  if (nInitialSize > nMaxSize || nWorking > nMaxSize) {
    std::cerr << "Invalid initial/total/working numbers." << std::endl;
    exit(-1);
  }

  if (pWorking < 0.0 || pWorking > 1.0) {
    std::cerr << "Keep 0 < pWorking < 1." << std::endl;
    exit(-1);
  }

  if (pMiss < 0.0 || pMiss > 1.0) {
    std::cerr << "Keep 0 < pMiss < 1." << std::endl;
    exit(-1);
  }

  RandomNumber r(seed);

  std::vector<double> opWeights;
  opWeights.push_back(pInsert);
  opWeights.push_back(pLookup);
  opWeights.push_back(pRemove);
  WeightedSelector operations(seed, opWeights);

  std::vector<double> workingWeights;
  workingWeights.push_back(1.0 - pWorking);
  workingWeights.push_back(pWorking);
  WeightedSelector working(seed, workingWeights);

  std::vector<double> missWeights;
  missWeights.push_back(1.0 - pMiss);
  missWeights.push_back(pMiss);
  WeightedSelector miss(seed, missWeights);

  TestMap map(useCuckoo, nInitialSize);
  unsigned minElement = 0;
  unsigned maxElement = 0;
  unsigned opCode;
  unsigned current;
  unsigned barrier, nHot, nCold;
  bool success;
  Key* k;
  Value* v;
  for (unsigned i = 0; i < nOpCount; i++) {
    opCode = operations.next();

    switch (opCode) {
      case 0:
        // insert if allowed
        if (maxElement - minElement >= nMaxSize) {
          break;
        }

        current = maxElement++;
        k = new Key(current);
        v = new Value(current);
        success = map.insert(*k, v);
        if (!success) {
          std::cout << "Failed to insert " << current << std::endl;
          exit(-1);
        } else {
          // std::cout << "Inserted " << current << std::endl;
        }
        delete k;
        delete v;
        break;
      case 1:
        // lookup
        barrier = MIN(minElement + nWorking, maxElement);
        nHot = barrier - minElement;
        nCold = maxElement - barrier;
        if (miss.next()) {
          current = maxElement + r.next();
        } else if (working.next()) {
          current = minElement + r.nextInRange(nHot);
        } else {
          current = nCold ? barrier + r.nextInRange(nCold)
                          : minElement + r.nextInRange(nHot);
        }

        k = new Key(current);
        v = map.lookup(current);
        /*if (v == nullptr) {
          std::cout << "Failed to find " << current << std::endl;
        } else {
          std::cout << "Found " << current << std::endl;
        }*/
        delete k;
        break;
      case 2:
        // remove if allowed
        if (minElement >= maxElement) {
          break;
        }
        current = working.next() ? minElement++ : maxElement--;

        k = new Key(current);
        success = map.remove(current);
        if (!success) {
          std::cout << "Failed to remove " << current << std::endl;
          exit(-1);
        } else {
          // std::cout << "Removed " << current << std::endl;
        }
        delete k;
        break;
      default:
        break;
    }
  }

  // std::cout << "Done." << std::endl;

  exit(0);
}
