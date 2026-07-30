#include "moe/Types.h"

#include <cctype>
#include <cstring>
#include <string>

namespace moe
{

const char* toName (Articulation a) noexcept
{
    switch (a)
    {
        case Articulation::sustain:       return "sustain";
        case Articulation::legato:        return "legato";
        case Articulation::staccato:      return "staccato";
        case Articulation::spiccato:      return "spiccato";
        case Articulation::pizzicato:     return "pizzicato";
        case Articulation::tremolo:       return "tremolo";
        case Articulation::trill:         return "trill";
        case Articulation::marcato:       return "marcato";
        case Articulation::sulPonticello: return "sul ponticello";
        case Articulation::sulTasto:      return "sul tasto";
        case Articulation::colLegno:      return "col legno";
        case Articulation::harmonics:     return "harmonics";
        case Articulation::muted:         return "muted";
        case Articulation::flutter:       return "flutter";
        case Articulation::swell:         return "swell";
        case Articulation::roll:          return "roll";
        case Articulation::hit:           return "hit";
        case Articulation::custom:        return "custom";
        default:                          return "?";
    }
}

const char* toName (Family f) noexcept
{
    switch (f)
    {
        case Family::strings:    return "strings";
        case Family::brass:      return "brass";
        case Family::woodwinds:  return "woodwinds";
        case Family::percussion: return "percussion";
        case Family::choir:      return "choir";
        case Family::other:      return "other";
        default:                 return "?";
    }
}

Articulation articulationFromLabel (const char* label) noexcept
{
    if (label == nullptr)
        return Articulation::custom;

    std::string s;
    for (const char* p = label; *p != '\0'; ++p)
        if (std::isalpha (static_cast<unsigned char> (*p)) != 0)
            s += static_cast<char> (std::tolower (static_cast<unsigned char> (*p)));

    auto has = [&s] (const char* needle) { return s.find (needle) != std::string::npos; };

    // Order matters: more specific labels are tested before their substrings.
    if (has ("sulpont") || has ("ponti"))          return Articulation::sulPonticello;
    if (has ("sultasto"))                          return Articulation::sulTasto;
    if (has ("collegno"))                          return Articulation::colLegno;
    if (has ("pizz"))                              return Articulation::pizzicato;
    if (has ("spicc"))                             return Articulation::spiccato;
    if (has ("stacc") || has ("short"))            return Articulation::staccato;
    if (has ("trem"))                              return Articulation::tremolo;
    if (has ("trill"))                             return Articulation::trill;
    if (has ("marc") || has ("accent"))            return Articulation::marcato;
    if (has ("harm") || has ("flageo"))            return Articulation::harmonics;
    if (has ("legato") || has ("leg"))             return Articulation::legato;
    if (has ("flutter") || has ("frull"))          return Articulation::flutter;
    if (has ("mute") || has ("sord") || has ("stopped") || has ("cuivre"))
        return Articulation::muted;
    if (has ("swell") || has ("cresc"))            return Articulation::swell;
    if (has ("roll"))                              return Articulation::roll;
    if (has ("hit") || has ("impact") || has ("oneshot"))
        return Articulation::hit;
    if (has ("sus") || has ("long") || has ("arco"))
        return Articulation::sustain;

    return Articulation::custom;
}

} // namespace moe
