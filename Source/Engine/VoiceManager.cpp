#include "Engine/VoiceManager.h"

namespace aurel::engine
{

void VoiceManager::prepare (double sampleRate) noexcept
{
    for (auto& v : voices)
        v.prepare (sampleRate);
    reset();
}

void VoiceManager::reset() noexcept
{
    for (auto& v : voices)
        v.reset();
    sustainDown = false;
    nextAge = 0;
}

void VoiceManager::handleEvent (const Event& e) noexcept
{
    switch (e.type)
    {
        case EventType::NoteOn:
            if (e.value > 0.0f)
                noteOn (e.note, e.channel, e.value);
            else
                noteOff (e.note, e.channel);
            break;

        case EventType::NoteOff:
            noteOff (e.note, e.channel);
            break;

        case EventType::SustainPedal:
            setSustain (e.value >= 0.5f);
            break;

        case EventType::AllNotesOff:
            for (auto& v : voices)
            {
                v.keyDown = false;
                v.sustained = false;
                v.release();
            }
            break;

        case EventType::AllSoundOff:
            for (auto& v : voices)
                v.reset();
            break;
    }
}

void VoiceManager::render (float* left, float* right, int numSamples) noexcept
{
    for (auto& v : voices)
        if (v.isActive())
            v.render (left, right, numSamples);
}

int VoiceManager::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.isActive() ? 1 : 0;
    return n;
}

void VoiceManager::noteOn (int note, int channel, float velocity) noexcept
{
    chooseVoiceForNoteOn().start (note, channel, velocity, nextAge++);
}

void VoiceManager::noteOff (int note, int channel) noexcept
{
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.isActive() && v.keyDown && v.getNote() == note && v.getChannel() == channel)
            if (oldest == nullptr || v.getAge() < oldest->getAge())
                oldest = &v;

    if (oldest == nullptr)
        return;

    oldest->keyDown = false;
    if (sustainDown)
        oldest->sustained = true;
    else
        oldest->release();
}

void VoiceManager::setSustain (bool down) noexcept
{
    sustainDown = down;
    if (down)
        return;

    for (auto& v : voices)
    {
        if (v.isActive() && v.sustained && ! v.keyDown)
        {
            v.sustained = false;
            v.release();
        }
    }
}

Voice& VoiceManager::chooseVoiceForNoteOn() noexcept
{
    Voice* oldestReleasing = nullptr;
    Voice* oldestHeld = nullptr;

    for (auto& v : voices)
    {
        switch (v.getState())
        {
            case Voice::State::Idle:
                return v;
            case Voice::State::Releasing:
                if (oldestReleasing == nullptr || v.getAge() < oldestReleasing->getAge())
                    oldestReleasing = &v;
                break;
            case Voice::State::Active:
                if (oldestHeld == nullptr || v.getAge() < oldestHeld->getAge())
                    oldestHeld = &v;
                break;
        }
    }

    return oldestReleasing != nullptr ? *oldestReleasing : *oldestHeld;
}

} // namespace aurel::engine
