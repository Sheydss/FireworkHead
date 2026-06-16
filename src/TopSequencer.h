#pragma once

#include <Arduino.h>
#include <functional>
#include "Top.h"

// ─────────────────────────────────────────────────────────────────────────────
//  TopSequencer
//
//  Runs through an array of Top entries sorted by timeInTenths.
//  When the elapsed time reaches a Top's timeInTenths value, the
//  onFire callback is called with that Top's id.
//
//  Time unit: tenths of a second (100ms per unit).
//
//  Usage
//    TopSequencer seq;
//    seq.onFire([](int id) { Serial.printf("Fire %d\n", id); });
//    seq.load(tops, count);
//    seq.start();
//
//    // In loop():
//    seq.update();
//
//    seq.pause();
//    seq.resume();
//    seq.stop();
// ─────────────────────────────────────────────────────────────────────────────

class TopSequencer
{
public:
    using OnFire = std::function<void(int topId)>;

    // ── Callback registration ─────────────────────────────────────────────────

    /// Called when a Top's timeInTenths is reached.
    void onFire(OnFire callback);

    // ── Load ──────────────────────────────────────────────────────────────────

    /// Point the sequencer at an existing Top array.
    /// The array must remain valid for the lifetime of the sequencer.
    /// Tops do not need to be pre-sorted — load() sorts them by timeInTenths.
    void load(Top* tops, uint16_t count);

    // ── Playback control ──────────────────────────────────────────────────────

    /// Start the sequence from the beginning.
    void start();

    /// Pause — elapsed time is frozen.
    void pause();

    /// Resume from where it was paused.
    void resume();

    /// Stop and reset to the beginning.
    void stop();

    /// Jump to a specific time (in tenths of a second).
    void jumpTo(int timeInTenths);

    // ── State queries ─────────────────────────────────────────────────────────
    bool isRunning() const;
    bool isPaused()  const;
    int  elapsedTenths() const; ///< Current elapsed time in tenths of a second

    // ── Must be called every loop() ───────────────────────────────────────────
    void update();

private:
    OnFire   m_onFire;

    Top*     m_tops       = nullptr;
    uint16_t m_count      = 0;
    uint16_t m_nextIndex  = 0;   ///< Index of the next Top to fire

    bool     m_running    = false;
    bool     m_paused     = false;

    uint32_t m_startMs    = 0;   ///< millis() when sequence started
    uint32_t m_pausedMs   = 0;   ///< millis() when pause began
    uint32_t m_offsetMs   = 0;   ///< Accumulated paused time (subtracted from elapsed)

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// Sort m_tops by timeInTenths ascending (simple insertion sort —
    /// array is small so no need for a heavier algorithm).
    void sortTops();

    /// Advance m_nextIndex past any Tops whose time is <= current elapsed.
    /// Used after jumpTo() to skip already-passed Tops.
    void syncNextIndex(int currentTenths);
};