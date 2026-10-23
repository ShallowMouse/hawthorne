#pragma once

#include <juce_core/juce_core.h>
#include "Marker.h"
#include <vector>
#include <memory>

//==============================================================================
// Manages collection of markers with auto-sorting and navigation
class MarkerManager
{
public:
    MarkerManager() = default;
    ~MarkerManager() = default;
    
    // Marker creation
    int addMarker(double timestamp, const juce::String& label = "")
    {
        auto marker = std::make_unique<Marker>(timestamp, label);
        markers.push_back(std::move(marker));
        
        // Sort and renumber
        sortAndRenumberMarkers();
        
        // Find the new marker's number
        for (const auto& m : markers)
        {
            if (std::abs(m->getTimestamp() - timestamp) < 0.001)
                return m->getNumber();
        }
        
        return markers.size();
    }
    
    // Marker deletion
    bool removeMarker(int markerNumber)
    {
        for (auto it = markers.begin(); it != markers.end(); ++it)
        {
            if ((*it)->getNumber() == markerNumber)
            {
                markers.erase(it);
                sortAndRenumberMarkers();
                return true;
            }
        }
        return false;
    }
    
    bool removeMarkerAtTime(double timestamp, double tolerance = 0.1)
    {
        for (auto it = markers.begin(); it != markers.end(); ++it)
        {
            if (std::abs((*it)->getTimestamp() - timestamp) < tolerance)
            {
                markers.erase(it);
                sortAndRenumberMarkers();
                return true;
            }
        }
        return false;
    }
    
    void clearAllMarkers()
    {
        markers.clear();
    }
    
    // Navigation
    Marker* getMarkerByNumber(int number)
    {
        for (auto& marker : markers)
        {
            if (marker->getNumber() == number)
                return marker.get();
        }
        return nullptr;
    }
    
    Marker* getNextMarker(double currentTime)
    {
        for (auto& marker : markers)
        {
            if (marker->getTimestamp() > currentTime + 0.001)  // Slight tolerance
                return marker.get();
        }
        return nullptr;  // No markers ahead
    }
    
    Marker* getPreviousMarker(double currentTime)
    {
        Marker* prev = nullptr;
        for (auto& marker : markers)
        {
            if (marker->getTimestamp() < currentTime - 0.001)  // Slight tolerance
                prev = marker.get();
            else
                break;
        }
        return prev;
    }
    
    Marker* getClosestMarker(double time)
    {
        if (markers.empty())
            return nullptr;
        
        Marker* closest = markers[0].get();
        double minDist = std::abs(closest->getTimestamp() - time);
        
        for (auto& marker : markers)
        {
            double dist = std::abs(marker->getTimestamp() - time);
            if (dist < minDist)
            {
                minDist = dist;
                closest = marker.get();
            }
        }
        
        return closest;
    }
    
    // Access
    int getNumMarkers() const { return static_cast<int>(markers.size()); }
    
    const std::vector<std::unique_ptr<Marker>>& getMarkers() const { return markers; }
    
    Marker* getMarker(int index)
    {
        if (index >= 0 && index < markers.size())
            return markers[index].get();
        return nullptr;
    }
    
    // Session save/load
    juce::XmlElement* saveToXml() const
    {
        auto xml = std::make_unique<juce::XmlElement>("Markers");
        
        for (const auto& marker : markers)
        {
            auto markerXml = xml->createNewChildElement("Marker");
            markerXml->setAttribute("timestamp", marker->getTimestamp());
            markerXml->setAttribute("label", marker->getLabel());
            markerXml->setAttribute("colour", marker->getColour().toString());
        }
        
        return xml.release();
    }
    
    void loadFromXml(const juce::XmlElement* xml)
    {
        markers.clear();
        
        if (!xml)
            return;
        
        for (auto* markerXml : xml->getChildIterator())
        {
            if (markerXml->hasTagName("Marker"))
            {
                double timestamp = markerXml->getDoubleAttribute("timestamp", 0.0);
                juce::String label = markerXml->getStringAttribute("label", "");
                juce::String colourStr = markerXml->getStringAttribute("colour", "");
                
                auto marker = std::make_unique<Marker>(timestamp, label);
                
                if (colourStr.isNotEmpty())
                    marker->setColour(juce::Colour::fromString(colourStr));
                
                markers.push_back(std::move(marker));
            }
        }
        
        sortAndRenumberMarkers();
    }
    
private:
    void sortAndRenumberMarkers()
    {
        // Sort by timestamp
        std::sort(markers.begin(), markers.end(),
                 [](const std::unique_ptr<Marker>& a, const std::unique_ptr<Marker>& b) {
                     return *a < *b;
                 });
        
        // Renumber sequentially
        for (size_t i = 0; i < markers.size(); ++i)
        {
            markers[i]->setNumber(static_cast<int>(i + 1));
        }
    }
    
    std::vector<std::unique_ptr<Marker>> markers;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerManager)
};
