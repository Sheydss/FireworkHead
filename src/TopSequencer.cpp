#include "TopSequencer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Callback registration
// ─────────────────────────────────────────────────────────────────────────────

void TopSequencer::onFire(OnFire callback)
{
    m_onFire = callback;
}

// ─────────────────────────────────────────────────────────────────────────────
//  load
// ─────────────────────────────────────────────────────────────────────────────

void TopSequencer::load(Top* tops, uint16_t count)
{
    m_tops      = tops;
    m_count     = count;
    m_nextIndex = 0;

    sortTops();

    Serial.printf("[Sequencer] Loaded %u tops\n", count);
    for (uint16_t i = 0; i < count; ++i)
        Serial.printf("  [%u] id=%d  t=%d tenths\n",
                      i, m_tops[i].id, m_tops[i].timeInTenths);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Playback control
// ─────────────────────────────────────────────────────────────────────────────

void TopSequencer::start()
{
    if (!m_tops || m_count == 0)
    {
        Serial.println("[Sequencer] start() called with no tops loaded");
        return;
    }

    m_startMs   = millis();
    m_offsetMs  = 0;
    m_pausedMs  = 0;
    m_nextIndex = 0;
    m_running   = true;
    m_paused    = false;

    Serial.println("[Sequencer] Started");
}

void TopSequencer::pause()
{
    if (!m_running || m_paused)
        return;

    m_pausedMs = millis();
    m_paused   = true;

    Serial.printf("[Sequencer] Paused at %d tenths\n", elapsedTenths());
}

void TopSequencer::resume()
{
    if (!m_running || !m_paused)
        return;

    // Accumulate the time spent paused so elapsed() stays accurate
    m_offsetMs += millis() - m_pausedMs;
    m_pausedMs  = 0;
    m_paused    = false;

    Serial.printf("[Sequencer] Resumed at %d tenths\n", elapsedTenths());
}

void TopSequencer::stop()
{
    m_running   = false;
    m_paused    = false;
    m_nextIndex = 0;
    m_offsetMs  = 0;
    m_pausedMs  = 0;

    Serial.println("[Sequencer] Stopped");
}

void TopSequencer::jumpTo(int timeInTenths)
{
    if (!m_running)
        return;

    // Rebase the start time so elapsedTenths() returns timeInTenths
    const uint32_t targetMs = static_cast<uint32_t>(timeInTenths) * 100;
    m_startMs  = millis() - targetMs;
    m_offsetMs = 0;

    if (m_paused)
        m_pausedMs = millis();

    // Advance or rewind the next-index pointer
    syncNextIndex(timeInTenths);

    Serial.printf("[Sequencer] Jumped to %d tenths\n", timeInTenths);
}

// ─────────────────────────────────────────────────────────────────────────────
//  State queries
// ─────────────────────────────────────────────────────────────────────────────

bool TopSequencer::isRunning() const { return m_running && !m_paused; }
bool TopSequencer::isPaused()  const { return m_running && m_paused; }

int TopSequencer::elapsedTenths() const
{
    if (!m_running)
        return 0;

    // While paused, time is frozen at the moment pause() was called
    const uint32_t nowMs    = m_paused ? m_pausedMs : millis();
    const uint32_t elapsedMs = nowMs - m_startMs - m_offsetMs;

    return static_cast<int>(elapsedMs / 100);
}

// ─────────────────────────────────────────────────────────────────────────────
//  update  —  call every loop()
// ─────────────────────────────────────────────────────────────────────────────

void TopSequencer::update()
{
    if (!m_running || m_paused)
        return;

    if (!m_tops || m_nextIndex >= m_count)
        return;

    const int now = elapsedTenths();

    // Fire all tops whose time has been reached
    while (m_nextIndex < m_count &&
           m_tops[m_nextIndex].timeInTenths <= now)
    {
        const int id = m_tops[m_nextIndex].id;

        Serial.printf("[Sequencer] Fire  id=%d  t=%d  elapsed=%d\n",
                      id, m_tops[m_nextIndex].timeInTenths, now);

        if (m_onFire)
            m_onFire(id);

        ++m_nextIndex;
    }

    // Auto-stop when all tops have fired
    if (m_nextIndex >= m_count)
    {
        Serial.println("[Sequencer] Sequence complete");
        stop();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void TopSequencer::sortTops()
{
    // Insertion sort — O(n²) but fine for small arrays (< a few hundred tops)
    for (uint16_t i = 1; i < m_count; ++i)
    {
        Top    key = m_tops[i];
        int16_t j  = static_cast<int16_t>(i) - 1;

        while (j >= 0 && m_tops[j].timeInTenths > key.timeInTenths)
        {
            m_tops[j + 1] = m_tops[j];
            --j;
        }
        m_tops[j + 1] = key;
    }
}

void TopSequencer::syncNextIndex(int currentTenths)
{
    // Scan from the beginning to find the first Top not yet past
    m_nextIndex = 0;
    while (m_nextIndex < m_count &&
           m_tops[m_nextIndex].timeInTenths <= currentTenths)
    {
        ++m_nextIndex;
    }
}