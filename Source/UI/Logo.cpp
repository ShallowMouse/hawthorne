#include "HawthorneLookAndFeel.h"
#include "BinaryData.h"

namespace hawthorne
{

juce::Font monoFont (float size, bool bold)
{
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    // the same type as every other part inside MELVILLE
    static auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeRegular_ttf, BinaryData::CourierPrimeRegular_ttfSize);
    static auto boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeBold_ttf, BinaryData::CourierPrimeBold_ttfSize);
    return juce::Font (juce::FontOptions (bold ? boldFace : regular).withHeight (size));
   #else
    return juce::Font (juce::FontOptions ("Courier New", size, bold ? juce::Font::bold : juce::Font::plain));
   #endif
}

juce::Font bodyFont (float size, bool bold)
{
   #if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
    static auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoRegular_ttf, BinaryData::RobotoRegular_ttfSize);
    static auto boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::RobotoBold_ttf, BinaryData::RobotoBold_ttfSize);
    return juce::Font (juce::FontOptions (bold ? boldFace : regular).withHeight (size));
   #else
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
   #endif
}

void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> area)
{
    static std::unique_ptr<juce::Drawable> mark = [] () -> std::unique_ptr<juce::Drawable>
    {
        if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::hawthornemark_svg,
                                                                         BinaryData::hawthornemark_svgSize)))
            return juce::Drawable::createFromSVG (*xml);
        return nullptr;
    }();

    if (mark != nullptr)
        mark->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
}

} // namespace hawthorne
