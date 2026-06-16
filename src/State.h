enum State
{
    NotConnected=-1,
    Preparation,
    Armement,
    Play,
    Pause,
    Stop,
    Fin
};

static const String StateToString(State s) {
    switch(s) {
        case State::NotConnected: return "NotConnected";
        case State::Preparation: return "Preparation";
        case State::Armement: return "Armement";
        case State::Play: return "Play";
        case State::Pause: return "Pause";
        case State::Stop: return "Stop";
        case State::Fin: return "Fin";
    }
    return "";
}