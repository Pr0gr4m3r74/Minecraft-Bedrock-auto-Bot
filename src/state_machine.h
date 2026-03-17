// state_machine.h – Bot finite-state machine state definitions.
//
// All states are transitions the Bot can be in at any given time.
// The state machine is driven by Bot::Run() on the worker thread.
//
// Windows-only; compiled with MSVC targeting Windows 10/11 x64.

#pragma once

/// States of the farm-bot finite-state machine.
///
/// Transitions (happy path):
///   Idle → ResetPosition → MoveToRowStart → FarmRow
///        → ReturnToOrigin → WaitForGrowth → ResetPosition (cycle)
///
/// Any state may transition to ErrorRecovery on unexpected conditions.
/// ErrorRecovery always falls back to Idle after the recovery attempt.
enum class BotState {
    Idle,           ///< Bot not running – worker thread is not active.
    ResetPosition,  ///< Verifying current position matches saved origin;
                    ///  navigates back to origin when a deviation is detected.
    MoveToRowStart, ///< Travelling (BFS path) to the first cell of the next row.
    FarmRow,        ///< Harvesting and replanting every cell in the current row.
    ReturnToOrigin, ///< Returning to the origin after a reset-interval or full cycle.
    WaitForGrowth,  ///< Timer wait for crops to mature before the next cycle.
    ErrorRecovery,  ///< Handling an unexpected condition; attempts to return to Idle.
};

/// Return a human-readable German label for each state.
inline const wchar_t* BotStateName(BotState s) noexcept {
    switch (s) {
    case BotState::Idle:           return L"Leerlauf";
    case BotState::ResetPosition:  return L"Position prüfen";
    case BotState::MoveToRowStart: return L"Zur Reihe";
    case BotState::FarmRow:        return L"Reihe ernten";
    case BotState::ReturnToOrigin: return L"Zum Ursprung";
    case BotState::WaitForGrowth:  return L"Warten auf Wachstum";
    case BotState::ErrorRecovery:  return L"Fehlerkorrektur";
    default:                       return L"Unbekannt";
    }
}
