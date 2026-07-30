#include "moe/voice/VoiceManager.h"

#include <algorithm>

namespace moe::voice
{

namespace
{
/** Below this a voice is inaudible in an orchestral mix and is fair game. */
constexpr float kStealableLevel = 0.02f;   // ~ -34 dB
}

void VoiceManager::prepare (double sampleRate, int blockSize, int maxVoices)
{
    voices.clear();
    voices.resize (static_cast<std::size_t> (std::max (1, maxVoices)));

    for (auto& voice : voices)
        voice.prepare (sampleRate, blockSize);

    startCounter = 0;
    stealCount   = 0;
}

Voice* VoiceManager::findFreeVoice()
{
    for (auto& voice : voices)
        if (! voice.isActive())
            return &voice;

    return nullptr;
}

Voice* VoiceManager::stealVoice()
{
    Voice* oldestReleasing = nullptr;
    Voice* quietest        = nullptr;
    Voice* oldest          = nullptr;

    for (auto& voice : voices)
    {
        if (! voice.isActive())
            return &voice;

        if (voice.isReleasing()
            && (oldestReleasing == nullptr
                || voice.getStartOrder() < oldestReleasing->getStartOrder()))
            oldestReleasing = &voice;

        if (quietest == nullptr || voice.getCurrentLevel() < quietest->getCurrentLevel())
            quietest = &voice;

        if (oldest == nullptr || voice.getStartOrder() < oldest->getStartOrder())
            oldest = &voice;
    }

    ++stealCount;

    if (oldestReleasing != nullptr)
        return oldestReleasing;

    if (quietest != nullptr && quietest->getCurrentLevel() < kStealableLevel)
        return quietest;

    return oldest;
}

Voice* VoiceManager::startVoice (const VoiceStartInfo& info)
{
    Voice* voice = findFreeVoice();

    if (voice == nullptr)
    {
        voice = stealVoice();
        if (voice == nullptr)
            return nullptr;

        // The stolen voice must give its streaming slot back before the new note
        // asks for one, or a busy pool would starve the very note that stole it.
        voice->reset();
    }

    voice->start (info, streamManager);
    voice->setStartOrder (++startCounter);
    return voice;
}

void VoiceManager::noteOff (int channel, int note, bool sustainDown)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.getMidiNote() != note
            || voice.getMidiChannel() != channel || voice.isReleasing())
            continue;

        if (sustainDown)
            voice.setSustained (true);
        else
            voice.noteOff();
    }
}

void VoiceManager::releaseSustained (int channel)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.isSustained() && voice.getMidiChannel() == channel)
        {
            voice.setSustained (false);
            voice.noteOff();
        }
}

void VoiceManager::silenceGroup (int sectionId, int group, float fadeSeconds)
{
    if (group == 0)
        return;

    for (auto& voice : voices)
        if (voice.isActive() && voice.getSectionId() == sectionId
            && voice.getExclusiveGroup() == group)
            voice.fadeOutAndStop (fadeSeconds);
}

void VoiceManager::stopSection (int sectionId, bool immediately)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.getSectionId() != sectionId)
            continue;

        if (immediately)
            voice.reset();
        else
            voice.fadeOutAndStop();
    }
}

void VoiceManager::allNotesOff (bool immediately)
{
    for (auto& voice : voices)
    {
        if (! voice.isActive())
            continue;

        if (immediately)
            voice.reset();
        else
            voice.noteOff();
    }
}

void VoiceManager::renderAdd (float* const* output, int numSamples)
{
    for (auto& voice : voices)
        if (voice.isActive())
            voice.renderAdd (output, numSamples);
}

void VoiceManager::renderSection (int sectionId, float* const* output, int numSamples)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.getSectionId() == sectionId)
            voice.renderAdd (output, numSamples);
}

void VoiceManager::releaseNote (int sectionId, int note, float releaseSeconds)
{
    for (auto& voice : voices)
        if (voice.isActive() && ! voice.isReleasing()
            && voice.getSectionId() == sectionId && voice.getMidiNote() == note)
            voice.fadeOutAndStop (releaseSeconds);
}

void VoiceManager::setDynamics (int sectionId, float value)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.getSectionId() == sectionId)
            voice.setDynamics (value);
}

void VoiceManager::setExpression (int sectionId, float value)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.getSectionId() == sectionId)
            voice.setExpression (value);
}

void VoiceManager::setSectionGain (int sectionId, float linearGain)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.getSectionId() == sectionId)
            voice.setSectionGain (linearGain);
}

void VoiceManager::setPitchBend (int sectionId, float semitones)
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.getSectionId() == sectionId)
            voice.setPitchBendSemitones (semitones);
}

int VoiceManager::getNumActiveVoices() const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;

    return count;
}

} // namespace moe::voice
