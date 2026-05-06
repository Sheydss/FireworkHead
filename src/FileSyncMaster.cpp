#include "FileSyncMaster.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / begin
// ─────────────────────────────────────────────────────────────────────────────

FileSyncMaster::FileSyncMaster(RS485Master& master, const char* planDir)
    : m_master(master)
    , m_planDir(planDir)
{}

void FileSyncMaster::begin()
{
    // Create the plan directory if it does not exist yet
    if (!LittleFS.exists(m_planDir))
        LittleFS.mkdir(m_planDir);

    Serial.printf("[SyncMaster] Ready  dir=%s\n", m_planDir.c_str());

    // ── Hook into the master's application-level command stream ──────────────
    // The master calls this lambda for every frame that is not handled
    // internally (i.e. not ping/ack/nack/file_*/hello/welcome/goodbye).
    m_master.onCommand([this](uint8_t srcId, const char* cmd, JsonDocument& doc)
    {
        if (strcmp(cmd, CMD_SYNC_REQUEST) == 0)
        {
            handleSyncRequest(srcId);
        }
        else if (strcmp(cmd, CMD_SYNC_NEED) == 0)
        {
            handleSyncNeed(srcId, doc);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
//  pushToAll / pushToNode
// ─────────────────────────────────────────────────────────────────────────────

void FileSyncMaster::pushToAll()
{
    const SlaveNode* active[NODE_MAX_NODES];
    const uint8_t    count =
        m_master.registry().activeNodes(active, NODE_MAX_NODES);

    if (count == 0)
    {
        Serial.println("[SyncMaster] pushToAll — no active nodes");
        return;
    }

    // Build the list once, send it to every node
    JsonDocument list;
    if (!buildFileList(list))
    {
        Serial.println("[SyncMaster] pushToAll — failed to build file list");
        return;
    }

    Serial.printf("[SyncMaster] Pushing file list to %u node(s)\n", count);
    for (uint8_t i = 0; i < count; ++i)
        sendList(active[i]->id, list);
}

void FileSyncMaster::pushToNode(uint8_t nodeId)
{
    JsonDocument list;
    if (!buildFileList(list))
    {
        Serial.printf("[SyncMaster] pushToNode 0x%02X — failed to build list\n",
                      nodeId);
        return;
    }
    sendList(nodeId, list);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — buildFileList
// ─────────────────────────────────────────────────────────────────────────────

bool FileSyncMaster::buildFileList(JsonDocument& out) const
{
    File dir = LittleFS.open(m_planDir);
    if (!dir || !dir.isDirectory())
    {
        Serial.printf("[SyncMaster] Cannot open plan dir: %s\n",
                      m_planDir.c_str());
        return false;
    }

    JsonArray files = out["files"].to<JsonArray>();

    File entry = dir.openNextFile();
    while (entry)
    {
        const String name = String(entry.name());

        // Only process JSON files
        if (name.endsWith(".json"))
        {
            const String path = m_planDir + "/" + name;
            const String hash = readHash(path);

            if (hash.isEmpty())
            {
                Serial.printf("[SyncMaster] No hash in %s — skipped\n",
                              name.c_str());
            }
            else
            {
                JsonObject obj = files.add<JsonObject>();
                obj["name"]    = name;
                obj["hash"]    = hash;
                Serial.printf("[SyncMaster] Listed  %s  hash=%s\n",
                              name.c_str(), hash.c_str());
            }
        }
        entry = dir.openNextFile();
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — readHash
// ─────────────────────────────────────────────────────────────────────────────

String FileSyncMaster::readHash(const String& path) const
{
    File f = LittleFS.open(path, "r");
    if (!f)
        return String();

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        Serial.printf("[SyncMaster] JSON parse error in %s: %s\n",
                      path.c_str(), err.c_str());
        return String();
    }

    const char* hash = doc["hash"] | "";
    return String(hash);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — sendList
// ─────────────────────────────────────────────────────────────────────────────

void FileSyncMaster::sendList(uint8_t nodeId, JsonDocument& list)
{
    // Inject the command name then serialise
    list["cmd"] = CMD_SYNC_LIST;

    String json;
    serializeJson(list, json);

    if (json.length() > RS485_MAX_PAYLOAD)
    {
        Serial.printf("[SyncMaster] sync_list too large (%zu B) for node 0x%02X"
                      " — split not implemented yet\n",
                      json.length(), nodeId);
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, json);  // re-parse to use sendCommand
    m_master.sendCommand(nodeId, doc, RS485_TIMEOUT_MS * 2);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — handleSyncRequest
// ─────────────────────────────────────────────────────────────────────────────

void FileSyncMaster::handleSyncRequest(uint8_t srcId)
{
    Serial.printf("[SyncMaster] sync_request from node 0x%02X\n", srcId);
    pushToNode(srcId);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private — handleSyncNeed
// ─────────────────────────────────────────────────────────────────────────────

void FileSyncMaster::handleSyncNeed(uint8_t srcId, JsonDocument& doc)
{
    JsonArray needed = doc["files"].as<JsonArray>();
    if (needed.isNull())
    {
        Serial.printf("[SyncMaster] sync_need from 0x%02X has no files array\n",
                      srcId);
        return;
    }

    Serial.printf("[SyncMaster] Node 0x%02X needs %u file(s)\n",
                  srcId, needed.size());

    for (JsonVariant v : needed)
    {
        const String name = v.as<String>();
        const String path = m_planDir + "/" + name;

        File f = LittleFS.open(path, "r");
        if (!f)
        {
            Serial.printf("[SyncMaster] File not found: %s\n", path.c_str());
            continue;
        }

        // Read the file into a heap buffer then send it
        const size_t fileSize = f.size();
        uint8_t*     buf      = static_cast<uint8_t*>(malloc(fileSize));

        if (!buf)
        {
            Serial.printf("[SyncMaster] No memory to send %s\n", name.c_str());
            f.close();
            continue;
        }

        f.read(buf, fileSize);
        f.close();

        Serial.printf("[SyncMaster] Sending %s (%zu B) to node 0x%02X\n",
                      name.c_str(), fileSize, srcId);

        m_master.sendFile(srcId, name.c_str(), buf, fileSize);
        free(buf);
    }
}