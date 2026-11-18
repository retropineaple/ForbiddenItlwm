// The station scan-result cursor (include/ItlScanCursor.hpp), audit ST02/T02: one result per request,
// no node pointer kept between requests, and a tree that changes between them.
#include <ItlScanCursor.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
using namespace ItlScan;

struct Node { uint8_t mac[6]; };
// Stands in for the node tree: ordered by address, nodes allocated and freed individually so the
// sanitizer catches any use of a freed one.
struct Tree {
    std::map<std::vector<uint8_t>, Node *> nodes;
    ~Tree() { for (auto &e : nodes) delete e.second; }
    void add(uint8_t last) {
        auto *n = new Node{{0x02, 0, 0, 0, 0, last}};
        nodes[std::vector<uint8_t>(n->mac, n->mac + 6)] = n;
    }
    void remove(uint8_t last) {
        std::vector<uint8_t> key{0x02, 0, 0, 0, 0, last};
        auto it = nodes.find(key);
        delete it->second;
        nodes.erase(it);
    }
    Node *first() const { return nodes.empty() ? nullptr : nodes.begin()->second; }
    Node *next(Node *n) const {
        auto it = nodes.upper_bound(std::vector<uint8_t>(n->mac, n->mac + 6));
        return it == nodes.end() ? nullptr : it->second;
    }
};

// One request, as getSCAN_RESULT makes it. Returns the node's last address byte, or -1 for the end.
static int request(Cursor &c, Tree &t) {
    Node *n = nextResult<Node>(c, [&] { return t.first(); }, [&](Node *x) { return t.next(x); },
                               [](Node *x) -> const uint8_t * { return x->mac; });
    if (!n)
        return -1;
    noteReturned(c, n->mac, t.next(n) == nullptr);
    return n->mac[5];
}

int main() {
    {   // An empty tree ends at once, every time.
        Tree t; Cursor c{};
        assert(request(c, t) == -1 && request(c, t) == -1 && !c.started && !c.wrapping);
    }
    {   // Three nodes, in order, then the end, then the list starts over.
        Tree t; t.add(3); t.add(1); t.add(2); Cursor c{};
        assert(request(c, t) == 1 && request(c, t) == 2 && request(c, t) == 3);
        assert(c.wrapping && !c.started);
        assert(request(c, t) == -1 && !c.wrapping);
        assert(request(c, t) == 1);
    }
    {   // The node just returned is freed before the next request: the list goes on after its address.
        Tree t; t.add(1); t.add(2); t.add(3); Cursor c{};
        assert(request(c, t) == 1);
        t.remove(1);
        assert(request(c, t) == 2);
        t.remove(2); t.remove(3);
        // Everything after the cursor went away: the list ends, and the next one starts fresh.
        assert(request(c, t) == -1 && !c.started && !c.wrapping);
        t.add(9);
        assert(request(c, t) == 9 && request(c, t) == -1);
    }
    {   // Nodes added between requests: one before the cursor isn't repeated, one after it is returned.
        Tree t; t.add(2); t.add(4); Cursor c{};
        assert(request(c, t) == 2);
        t.add(1); t.add(3);
        assert(request(c, t) == 3 && request(c, t) == 4 && request(c, t) == -1);
    }
    {   // One node: it is returned, then the list ends.
        Tree t; t.add(7); Cursor c{};
        assert(request(c, t) == 7 && c.wrapping && request(c, t) == -1 && request(c, t) == 7);
    }
    {   // The comparison is over all six bytes, high bytes first.
        Tree t; Cursor c{};
        auto *a = new Node{{0x02, 0, 0, 0, 1, 0x00}}; t.nodes[std::vector<uint8_t>(a->mac, a->mac + 6)] = a;
        auto *b = new Node{{0x02, 0, 0, 0, 0, 0xff}}; t.nodes[std::vector<uint8_t>(b->mac, b->mac + 6)] = b;
        assert(request(c, t) == 0xff && request(c, t) == 0x00 && request(c, t) == -1);
    }
    puts("scan result cursor: passed");
}
