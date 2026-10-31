// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Render.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include "Json.h"

namespace a3core
{
namespace
{
using Pairs = std::vector<std::pair<std::string, std::string>>;

// f"{key}={value}\n" for each pair.
std::string lines (const Pairs& pairs)
{
    std::string text;
    for (const auto& pair : pairs)
        text += pair.first + "=" + pair.second + "\n";
    return text;
}

// str(value) of what json.loads made of a scalar: the network.json may hold a number where a text
// is expected, and the Python renderer wrote it as str() does.
std::string pythonStr (const juce::var& value)
{
    if (value.isBool())
        return static_cast<bool> (value) ? "True" : "False";
    if (value.isString())
        return value.toString().toStdString();
    if (value.isInt() || value.isInt64())
        return std::to_string (static_cast<juce::int64> (value));
    if (value.isDouble())
        return pythonRepr (static_cast<double> (value));
    if (value.isVoid())
        return "None";
    throw RenderError { "a list or an object where a text is due" };
}

juce::var required (const juce::var& block, const char* key, const char* blockName)
{
    if (! block.hasProperty (key))
        throw TruthError { std::string ("no '") + key + "' in the truth's " + blockName };
    return block[juce::Identifier (key)];
}

std::string portOf (const Truth& truth, const char* program, const char* role) { return std::to_string (truth.port (program, role)); }

std::string endpointText (const Truth& truth, const std::string& program, const std::string& role)
{
    const auto endpoint = truth.endpoint (program, role);
    return endpoint.host + ":" + std::to_string (endpoint.port);
}

std::string replaceAll (std::string text, const std::string& from, const std::string& to)
{
    for (size_t at = 0; (at = text.find (from, at)) != std::string::npos; at += to.size())
        text.replace (at, from.size(), to);
    return text;
}

void makeParent (const juce::File& file)
{
    if (! file.getParentDirectory().createDirectory())
        throw RenderError { "cannot create " + file.getParentDirectory().getFullPathName().toStdString() };
}

void writeText (const juce::File& file, const std::string& text)
{
    std::ofstream out (file.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
    out.write (text.data(), static_cast<std::streamsize> (text.size()));
    out.close();
    if (! out)
        throw RenderError { "cannot write " + file.getFullPathName().toStdString() };
}

// Path.read_text(): UTF-8, and \r\n or \r read as \n.
std::string readText (const juce::File& file)
{
    juce::MemoryBlock block;
    if (! file.loadFileAsData (block))
        throw RenderError { "cannot read " + file.getFullPathName().toStdString() };
    const auto* chars = static_cast<const char*> (block.getData());
    const auto size = static_cast<int> (block.getSize());
    if (! juce::CharPointer_UTF8::isValidString (chars, size))
        throw RenderError { "not UTF-8: " + file.getFullPathName().toStdString() };
    return replaceAll (replaceAll (std::string (chars, block.getSize()), "\r\n", "\n"), "\r", "\n");
}
}

bool carriesVu (const juce::var& route)
{
    return route["to"].toString().endsWith (".vu") || (route["carries"].isString() && route["carries"].toString() == "vu");
}

std::string oscEnv (const Truth& truth)
{
    return lines ({ { "A3_VNC_PORT", portOf (truth, "x11vnc", "vnc") },
                    { "A3_SCREENCAST_PORT", portOf (truth, "screencast", "stream") } });
}

std::string networkDefaults (const Truth& truth)
{
    const auto network = truth.network();
    const auto field = [&] (const char* key) { return pythonStr (required (network, key, "network")); };
    return lines ({ { "INTERFACE", field ("interface") },
                    { "ADDR", field ("address") },
                    { "GATEWAY", field ("gateway") },
                    { "DNS", field ("dns") },
                    { "AUDIO_PORT", field ("audio_port") },
                    { "AUDIO_ADDR", field ("audio_address") } });
}

std::string analyzerBlock (const Truth& truth)
{
    Pairs targets;
    for (const auto& route : truth.routes())
    {
        if (! route["from"].isString() || route["from"].toString() != "beat-analyzer" || carriesVu (route))
            continue;
        const auto to = route["to"].toString().toStdString();
        const auto dot = to.find ('.');
        if (dot == std::string::npos || to.find ('.', dot + 1) != std::string::npos)
            throw TruthError { "route target '" + to + "' is not program.role" };
        const auto program = to.substr (0, dot), role = to.substr (dot + 1);
        targets.push_back ({ "OSC_HOST_" + program, endpointText (truth, program, role) });
    }
    targets.push_back ({ "OSC_PORT_A3MOTION", portOf (truth, "beat-analyzer", "clock") });
    // Its words and the Pro DJ Link ports: beat-analyzer's Config::OscWords.
    targets.push_back ({ "OSC_ADDRESS_BEAT", truth.pattern ("beat") });
    targets.push_back ({ "OSC_ADDRESS_TAP", truth.pattern ("tap") });
    targets.push_back ({ "OSC_ADDRESS_CLOCKMODE", truth.pattern ("clockmode") });
    targets.push_back ({ "PIONEER_PORT_ANNOUNCE", portOf (truth, "prolink", "announce") });
    targets.push_back ({ "PIONEER_PORT_BEAT", portOf (truth, "prolink", "beat") });
    targets.push_back ({ "PIONEER_PORT_STATUS", portOf (truth, "prolink", "status") });
    return std::string (renderedHeader) + "\n" + lines (targets);
}

std::optional<std::string> aes67Conf (const Truth& truth, const std::string& hostname, const juce::File& aes67Template)
{
    const auto network = truth.network();
    const auto port = network.hasProperty ("audio_port") ? network["audio_port"] : juce::var (juce::String());
    if (! pythonTruthy (port))
        return std::nullopt;
    if (! port.isString())
        throw RenderError { "the audio port is not a text" };

    const auto stream = truth.endpoint ("aes67", "stream");
    const auto sap = truth.endpoint ("aes67", "sap");
    auto text = readText (aes67Template);
    const Pairs placeholders { { "@AUDIO_PORT@", port.toString().toStdString() },
                               { "@SENDER_NAME@", "AES67 " + hostname + " out" },
                               { "@STREAM_IP@", stream.host },
                               { "@STREAM_PORT@", std::to_string (stream.port) },
                               { "@SAP_IP@", sap.host },
                               { "@SAP_PORT@", std::to_string (sap.port) } };
    for (const auto& placeholder : placeholders)
        text = replaceAll (text, placeholder.first, placeholder.second);
    return std::string (renderedHeader) + "\n" + text;
}

bool isRendered (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;
    std::ifstream in (file.getFullPathName().toStdString(), std::ios::binary);
    // readline() with universal newlines: the first line ends at \n, \r\n or \r.
    const std::string header = renderedHeader;
    std::string first;
    for (char c; first.size() <= header.size() && in.get (c);)
    {
        if (c == '\n' || c == '\r')
            break;
        first += c;
    }
    return first == header;
}

void replaceWhole (const juce::File& file, const std::string& text)
{
    makeParent (file);
    const auto temporary = file.getSiblingFile ("." + file.getFileName() + ".tmp");
    writeText (temporary, text);
    std::error_code problem;
    std::filesystem::rename (temporary.getFullPathName().toStdString(), file.getFullPathName().toStdString(), problem);
    if (problem)
    {
        temporary.deleteFile();
        throw RenderError { "cannot replace " + file.getFullPathName().toStdString() + ": " + problem.message() };
    }
}

std::string writeAes67Conf (const Truth& truth, const juce::File& home, const std::string& hostname, const juce::File& aes67Template)
{
    const auto path = home.getChildFile (aes67ConfPath);
    const auto text = aes67Conf (truth, hostname, aes67Template);
    const std::string shown = std::string ("~/") + aes67ConfPath;
    if (path.exists() && ! isRendered (path))
        return "kept " + shown + ": not rendered by a3-osc-render";
    if (! text)
    {
        if (! path.exists())
            return "no audio port in the network: no AES67";
        if (! path.deleteFile())
            throw RenderError { "cannot remove " + path.getFullPathName().toStdString() };
        return "removed " + shown + ": no audio port in the network";
    }
    replaceWhole (path, *text);
    return "rendered " + shown;
}

std::string writeUserFiles (const Truth& truth, const juce::File& home, const std::string& hostname, const juce::File& aes67Template)
{
    const auto envFile = home.getChildFile (envFilePath);
    makeParent (envFile);
    writeText (envFile, oscEnv (truth));
    replaceWhole (home.getChildFile (analyzerConfPath), analyzerBlock (truth));
    return writeAes67Conf (truth, home, hostname, aes67Template);
}

std::string networkFile (const Truth& truth)
{
    const auto data = truth.data();
    auto* picked = new juce::DynamicObject();
    for (const auto* key : { "hosts", "network" })
        picked->setProperty (juce::Identifier (key), required (data, key, "file"));
    return dumpIndented (juce::var (picked), 1) + "\n";
}

bool writeNetworkFileOnce (const Truth& truth, const juce::File& path)
{
    makeParent (path);
    const auto text = networkFile (truth);
    const auto descriptor = ::open (path.getFullPathName().toRawUTF8(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor < 0)
    {
        if (errno == EEXIST)
            return false;
        throw RenderError { "cannot create " + path.getFullPathName().toStdString() + ": " + std::strerror (errno) };
    }
    size_t done = 0;
    while (done < text.size())
    {
        const auto n = ::write (descriptor, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        done += static_cast<size_t> (n);
    }
    ::close (descriptor);
    if (done < text.size())
        throw RenderError { "cannot write " + path.getFullPathName().toStdString() };
    return true;
}

std::optional<std::string> refusedNetworkNotice (const Truth& truth)
{
    if (! truth.networkProblem)
        return std::nullopt;
    return "network.json refused, the package's defaults are used: " + *truth.networkProblem + "\n";
}
}
