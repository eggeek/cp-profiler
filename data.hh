/*  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef DATA_HH
#define DATA_HH

#include <vector>
#include <unordered_map>
#include <QTimer>
#include <chrono>
#include <QMutex>

#include <iostream>
#include <string>

#include <QDebug>

#include <cstdint>
#include <cassert>

namespace message {
    class Node;
}

enum MsgType {
  NODE_DATA = 1,
  DONE_SENDING = 2,
  START_SENDING = 3
};

class DbEntry {

public:
    DbEntry(int sid, int restart_id, int64_t parent_id, int _alt, int _kids,
            std::string _label, int tid, int _status, unsigned long long _time_stamp,
            unsigned long long _node_time, float _domain, int _nogood_bld,
            bool _uses_assumptions, int _backjump_distance, int _decision_level) :
        s_node_id(sid), restart_id(restart_id), gid(-1), parent_sid(parent_id), alt(_alt), numberOfKids(_kids),
        status(_status), label(_label), thread_id(tid), depth(-1), time_stamp(_time_stamp), node_time(_node_time),
        domain(_domain), nogood_bld(_nogood_bld),
        usesAssumptions(_uses_assumptions),
        backjump_distance(_backjump_distance),
        decision_level(_decision_level)
    {
    }

    friend std::ostream& operator<<(std::ostream& s, const DbEntry& e);

    union {
        struct {
            int32_t s_node_id; // solver node id
            int32_t restart_id;
        };
        int64_t full_sid;
    };
    int gid; // gist id, set to -1 so we don't forget to assign the real value
    int64_t parent_sid; // TODO(maxim): this needs only 32 bit integer, as restart_id is known
    int alt; // which child by order
    int numberOfKids;
    int status;
    std::string label;
    int thread_id; 
    int depth;
    unsigned long long time_stamp;
    unsigned long long node_time;
    float domain;
    int nogood_bld;
    bool usesAssumptions;
    int backjump_distance;
    int decision_level;
};

class Data : public QObject {
Q_OBJECT

    using system_clock = std::chrono::system_clock;

/// step for node rate counter (in microseconds)
    static constexpr int NODE_RATE_STEP = 1000;

    std::vector<DbEntry*> nodes_arr;

    /// counts instances of Data
    static int instance_counter;

    // Whether received DONE_SENDING message
    bool _isDone;

    // Name of the FlatZinc model
    std::string _title;

    // Total solver time in microseconds
    unsigned long long _total_time;

    unsigned long int _prev_node_timestamp;

    /// How many nodes received within each NODE_RATE_STEP interval
    std::vector<float> node_rate;

    /// derived properties
    int _time_per_node;

    /// for node rate
    system_clock::time_point begin_time;
    system_clock::time_point last_interval_time;
    system_clock::time_point current_time;

    int last_interval_nc;

    /// Map solver Id to no-good string
    std::unordered_map<int64_t, std::string> sid2nogood;
public:

    /// On which node each interval starts
    std::vector<int> nr_intervals;

    /// Maps gist Id to dbEntry (possibly in the other Data instance);
    /// i.e. needed for a merged tree to show labels etc.
    /// TODO(maixm): this should probably be a vector?
    std::unordered_map<int, DbEntry*> gid2entry;


    std::unordered_map<int64_t, std::string*> sid2info;

    /// Mapping from solver Id to array Id (nodes_arr)
    /// can't use vector because sid is too big with threads
    std::unordered_map<int64_t, int> sid2aid;

    /// used to access Data instance from different threads (in parallel solver)
    QMutex dataMutex;

private:
    // int _total_nodes;

    /// Populate nodes_arr with the data coming from
    void pushInstance(DbEntry* entry);

public:

    Data();
    ~Data(void);

    int handleNodeCallback(message::Node& node);

    /// TODO(maxim): Do I want a reference here?
    /// return label by gid (Gist ID)
    std::string getLabel(int gid);

    /// return solver id by gid (Gist ID)
    int64_t gid2sid(int gid);

    void connectNodeToEntry(int gid, DbEntry* const entry);

    /// return total number of nodes
    int size() const { return nodes_arr.size(); }

/// ********* GETTERS **********

    bool isDone(void) { return _isDone; }
    // bool isRestarts(void) { return _isRestarts; }
    std::string getTitle(void) { return _title; }
    const std::vector<DbEntry*>& getEntries() const { return nodes_arr; }
    inline const std::unordered_map<int64_t, std::string>& getNogoods(void) { return sid2nogood; }
    inline std::unordered_map<int64_t, std::string*>& getInfo(void) { return sid2info; }

    unsigned long long getTotalTime(void); /// time in microseconds

    int getGidBySid(int64_t sid) { return nodes_arr[sid2aid[sid]]->gid; }
    DbEntry* getEntry(int gid) const;


/// ****************************

/// ********* SETTERS **********

    void setTitle(std::string title) { _title = title; }


/// ****************************

    public Q_SLOTS:

    // sets _isDone to true when received DONE_SENDING
    void setDoneReceiving(void);

#ifdef MAXIM_DEBUG
    const std::string getDebugInfo() const;
#endif
};

inline
void Data::connectNodeToEntry(int gid, DbEntry* entry) {
    gid2entry[gid] = entry;
}

inline
DbEntry* Data::getEntry(int gid) const {
    auto it = gid2entry.find(gid);
    if (it != gid2entry.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}


#endif // DATA_HH

