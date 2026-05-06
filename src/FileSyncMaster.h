#pragma once

#include "RS485Master.h"

#include <FS.h>
#include <LittleFS.h>

// ─────────────────────────────────────────────────────────────────────────────
//  FileSyncMaster
//
//  Runs on the Heltec board.
//
//  Responsibilities:
//    - Respond to sync_request from a slave with the list of JSON plans
//      stored in LittleFS (name + hash read from the "hash" field)
//    - Respond to sync_need by sending each requested file via the existing
//      RS485Master::sendFile() chunked transfer
//
//  The Heltec is the single source of truth.  Slaves mirror it exactly.
//
//  Usage
//    FileSyncMaster sync(master);
//    sync.begin();                // call once in setup() after master.begin()
//
//    // In loop() — nothing needed, sync hooks into master.onCommand
//
//    // When a new plan is uploaded to the Heltec via the mobile app:
//    sync.pushToAll();            // push new file list to every active node
// ─────────────────────────────────────────────────────────────────────────────

class FileSyncMaster
{
public:
    /// @param master   The RS485Master instance.
    /// @param planDir  LittleFS directory that contains the JSON plan files.
    explicit FileSyncMaster(RS485Master& master,
                            const char*  planDir = "/plans");

    /// Mount LittleFS and register command hooks on the master.
    /// Call once in setup(), after master.begin().
    void begin();

    /// Push the current file list to every active slave so they can
    /// self-sync without having to send a sync_request first.
    /// Call this whenever a new plan is stored on the Heltec.
    void pushToAll();

    /// Push the file list to one specific slave.
    void pushToNode(uint8_t nodeId);

private:
    // ── Helpers ───────────────────────────────────────────────────────────────

    /// Read the "hash" field from a JSON file in LittleFS.
    /// Returns an empty String if the file cannot be opened or has no hash.
    String readHash(const String& path) const;

    /// Build a JsonDocument containing the array of {name, hash} entries
    /// for every .json file found in m_planDir.
    /// @return false if the directory cannot be opened.
    bool buildFileList(JsonDocument& out) const;

    /// Send the file list document to @p nodeId as a sync_list command.
    void sendList(uint8_t nodeId, JsonDocument& list);

    /// Handle an incoming sync_request from a slave.
    void handleSyncRequest(uint8_t srcId);

    /// Handle an incoming sync_need from a slave (list of filenames it wants).
    void handleSyncNeed(uint8_t srcId, JsonDocument& doc);

    // ── Members ───────────────────────────────────────────────────────────────
    RS485Master& m_master;
    String       m_planDir;
};