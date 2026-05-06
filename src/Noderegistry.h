#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NodeRegistry
//
//  Manages the table of known slave nodes on the master side.
//
//  Responsibilities:
//    - Assign a unique bus address (1–MAX_NODES) when a new slave announces
//    - Remember each node's MAC address so the same board always gets the
//      same ID across reconnections
//    - Track liveness: last-seen timestamp + missed-ping counter
//    - Mark nodes inactive after too many missed pings
//
//  Address space
//    0x00        reserved — master
//    0x01–0x1F   assignable slave addresses (MAX_NODES = 31)
//    0xFE        temporary address used by unregistered slaves
//    0xFF        broadcast
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t NODE_UNREGISTERED_ADDR = 0xFE; ///< Slave uses this before it has an ID
static constexpr uint8_t NODE_MAX_MISSED_PINGS   = 3;    ///< Pings missed before node is evicted
static constexpr uint8_t NODE_MAX_NODES          = 31;   ///< Maximum simultaneous slaves

// ─────────────────────────────────────────────────────────────────────────────
//  SlaveNode  –  one entry in the registry
// ─────────────────────────────────────────────────────────────────────────────

struct SlaveNode
{
    uint8_t  id           = 0;      ///< Assigned bus address (0 = slot empty)
    char     mac[18]      = {};     ///< MAC as "AA:BB:CC:DD:EE:FF"
    uint32_t lastSeenMs   = 0;      ///< millis() of last successful ping or hello
    uint8_t  missedPings  = 0;      ///< Consecutive unanswered pings
    bool     active       = false;  ///< true while the node is considered online
};

// ─────────────────────────────────────────────────────────────────────────────
//  NodeRegistry
// ─────────────────────────────────────────────────────────────────────────────

class NodeRegistry
{
public:
    // ── Callback signatures ───────────────────────────────────────────────────

    /// Called when a new slave is registered or a known slave reconnects.
    using OnNodeJoined = std::function<void(const SlaveNode& node)>;

    /// Called when a slave stops responding and is marked inactive.
    using OnNodeLost   = std::function<void(const SlaveNode& node)>;

    // ── Constructor ───────────────────────────────────────────────────────────
    NodeRegistry() = default;

    // ── Callback registration ─────────────────────────────────────────────────
    void onNodeJoined(OnNodeJoined callback);
    void onNodeLost  (OnNodeLost   callback);

    // ── Registration ─────────────────────────────────────────────────────────

    /// Called when a slave sends a "hello" with its MAC address.
    /// Finds an existing entry for that MAC or allocates a new slot.
    /// @return The assigned bus address, or 0 if the table is full.
    uint8_t registerNode(const char* mac);

    // ── Liveness ─────────────────────────────────────────────────────────────

    /// Mark a node as successfully seen right now (resets missed-ping counter).
    void markSeen(uint8_t id);

    /// Record one missed ping for a node.
    /// If NODE_MAX_MISSED_PINGS is reached the node is marked inactive
    /// and the OnNodeLost callback fires.
    void recordMissedPing(uint8_t id);

    // ── Queries ───────────────────────────────────────────────────────────────

    /// @return Pointer to a node entry, or nullptr if @p id is not registered.
    const SlaveNode* find(uint8_t id)   const;

    /// @return Pointer to a node entry by MAC, or nullptr if not found.
    const SlaveNode* findByMac(const char* mac) const;

    /// Fill @p out with pointers to all currently active nodes.
    /// @return Number of active nodes written into @p out.
    uint8_t activeNodes(const SlaveNode* out[], uint8_t maxOut) const;

    /// Total number of registered (active + inactive) entries.
    uint8_t count() const;

    /// Print the full table to Serial (for debugging).
    void dump() const;

private:
    SlaveNode    m_nodes[NODE_MAX_NODES] = {};
    OnNodeJoined m_onJoined;
    OnNodeLost   m_onLost;

    // ── Internal helpers ──────────────────────────────────────────────────────

    /// Find a mutable node by ID (returns nullptr if not found).
    SlaveNode* findMutable(uint8_t id);

    /// Allocate the next free bus address (1–NODE_MAX_NODES).
    /// Returns 0 if all slots are occupied.
    uint8_t allocateAddress() const;
};