#include "NodeRegistry.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Callback registration
// ─────────────────────────────────────────────────────────────────────────────

void NodeRegistry::onNodeJoined(OnNodeJoined callback) { m_onJoined = callback; }
void NodeRegistry::onNodeLost  (OnNodeLost   callback) { m_onLost   = callback; }

// ─────────────────────────────────────────────────────────────────────────────
//  registerNode
// ─────────────────────────────────────────────────────────────────────────────

uint8_t NodeRegistry::registerNode(const char* mac)
{
    // ── 1. Is this MAC already in the table? ──────────────────────────────────
    for (SlaveNode& node : m_nodes)
    {
        if (node.id == 0)
            continue; // empty slot

        if (strncmp(node.mac, mac, sizeof(node.mac)) == 0)
        {
            // Known node — mark it active again (reconnection)
            node.active      = true;
            node.missedPings = 0;
            node.lastSeenMs  = millis();

            Serial.printf("[Registry] Known node reconnected  MAC=%s  id=0x%02X\n",
                          mac, node.id);

            if (m_onJoined)
                m_onJoined(node);

            return node.id;
        }
    }

    // ── 2. New MAC — allocate a fresh address ─────────────────────────────────
    const uint8_t newId = allocateAddress();
    if (newId == 0)
    {
        Serial.printf("[Registry] Table full — cannot register MAC=%s\n", mac);
        return 0;
    }

    // Find the first empty slot (id == 0 means unused)
    for (SlaveNode& node : m_nodes)
    {
        if (node.id != 0)
            continue;

        node.id          = newId;
        node.active      = true;
        node.missedPings = 0;
        node.lastSeenMs  = millis();
        strlcpy(node.mac, mac, sizeof(node.mac));

        Serial.printf("[Registry] New node registered  MAC=%s  id=0x%02X\n",
                      mac, newId);

        if (m_onJoined)
            m_onJoined(node);

        return newId;
    }

    return 0; // should never reach here
}

// ─────────────────────────────────────────────────────────────────────────────
//  markSeen
// ─────────────────────────────────────────────────────────────────────────────

void NodeRegistry::markSeen(uint8_t id)
{
    SlaveNode* node = findMutable(id);
    if (!node)
        return;

    node->lastSeenMs  = millis();
    node->missedPings = 0;
    node->active      = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  recordMissedPing
// ─────────────────────────────────────────────────────────────────────────────

void NodeRegistry::recordMissedPing(uint8_t id)
{
    SlaveNode* node = findMutable(id);
    if (!node || !node->active)
        return;

    node->missedPings++;

    Serial.printf("[Registry] Node 0x%02X missed ping %u/%u\n",
                  id, node->missedPings, NODE_MAX_MISSED_PINGS);

    if (node->missedPings >= NODE_MAX_MISSED_PINGS)
    {
        node->active = false;
        Serial.printf("[Registry] Node 0x%02X  MAC=%s  marked LOST\n",
                      id, node->mac);

        if (m_onLost)
            m_onLost(*node);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Queries
// ─────────────────────────────────────────────────────────────────────────────

const SlaveNode* NodeRegistry::find(uint8_t id) const
{
    for (const SlaveNode& node : m_nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

const SlaveNode* NodeRegistry::findByMac(const char* mac) const
{
    for (const SlaveNode& node : m_nodes)
    {
        if (node.id != 0 && strncmp(node.mac, mac, sizeof(node.mac)) == 0)
            return &node;
    }
    return nullptr;
}

uint8_t NodeRegistry::activeNodes(const SlaveNode* out[], uint8_t maxOut) const
{
    uint8_t count = 0;
    for (const SlaveNode& node : m_nodes)
    {
        if (node.id != 0 && node.active)
        {
            if (count < maxOut)
                out[count++] = &node;
        }
    }
    return count;
}

uint8_t NodeRegistry::count() const
{
    uint8_t n = 0;
    for (const SlaveNode& node : m_nodes)
    {
        if (node.id != 0)
            ++n;
    }
    return n;
}

void NodeRegistry::dump() const
{
    Serial.printf("[Registry] ── Node table (%u entries) ──────────────\n", count());
    for (const SlaveNode& node : m_nodes)
    {
        if (node.id == 0)
            continue;
        Serial.printf("  id=0x%02X  mac=%-17s  %s  missed=%u  lastSeen=%lums ago\n",
                      node.id,
                      node.mac,
                      node.active ? "ONLINE " : "OFFLINE",
                      node.missedPings,
                      millis() - node.lastSeenMs);
    }
    Serial.println("[Registry] ────────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────────────────────

SlaveNode* NodeRegistry::findMutable(uint8_t id)
{
    for (SlaveNode& node : m_nodes)
    {
        if (node.id == id)
            return &node;
    }
    return nullptr;
}

uint8_t NodeRegistry::allocateAddress() const
{
    // Collect which addresses are already in use
    bool used[NODE_MAX_NODES + 1] = {};
    for (const SlaveNode& node : m_nodes)
    {
        if (node.id != 0 && node.id <= NODE_MAX_NODES)
            used[node.id] = true;
    }

    // Return the first free address starting at 1
    for (uint8_t addr = 1; addr <= NODE_MAX_NODES; ++addr)
    {
        if (!used[addr])
            return addr;
    }
    return 0; // table full
}