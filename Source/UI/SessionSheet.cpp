#include "SessionSheet.h"
#include "UIHelpers.h"

namespace hawthorne
{

namespace
{
    /** Paper-coloured look for the sheet's widgets. */
    struct PaperLookAndFeel : public juce::LookAndFeel_V4
    {
        PaperLookAndFeel()
        {
            using namespace juce;
            setColour (TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            setColour (TextEditor::textColourId, Paper::ink);
            setColour (TextEditor::outlineColourId, juce::Colours::transparentBlack);
            setColour (TextEditor::focusedOutlineColourId, Paper::margin.withAlpha (0.5f));
            setColour (TextEditor::highlightColourId, Paper::rule.withAlpha (0.5f));
            setColour (CaretComponent::caretColourId, Paper::ink);
            setColour (Label::textColourId, Paper::ink);
            setColour (Label::textWhenEditingColourId, Paper::ink);
            setColour (Label::backgroundWhenEditingColourId, juce::Colours::white.withAlpha (0.5f));
            setColour (Label::outlineWhenEditingColourId, Paper::margin.withAlpha (0.6f));
            setColour (ComboBox::backgroundColourId, juce::Colours::transparentBlack);
            setColour (ComboBox::textColourId, Paper::ink);
            setColour (ComboBox::outlineColourId, Paper::faint.withAlpha (0.5f));
            setColour (ComboBox::arrowColourId, Paper::ink);
            setColour (PopupMenu::backgroundColourId, Paper::paper);
            setColour (PopupMenu::textColourId, Paper::ink);
            setColour (PopupMenu::highlightedBackgroundColourId, Paper::ink);
            setColour (PopupMenu::highlightedTextColourId, Paper::paper);
            setColour (ListBox::backgroundColourId, Paper::paper);
            setColour (TableHeaderComponent::backgroundColourId, Paper::paperDark);
            setColour (TableHeaderComponent::textColourId, Paper::ink);
            setColour (TableHeaderComponent::outlineColourId, Paper::faint.withAlpha (0.5f));
            setColour (ScrollBar::thumbColourId, Paper::faint);
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return Paper::type (14.0f); }
        juce::Font getPopupMenuFont() override                { return Paper::type (15.0f); }
        juce::Font getLabelFont (juce::Label&) override       { return Paper::type (14.0f); }

        void drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& name, int,
                                    int w, int h, bool, bool, int) override
        {
            g.setColour (Paper::ink);
            g.setFont (Paper::type (12.0f, true));
            g.drawText (name, 6, 0, w - 8, h, juce::Justification::centredLeft);
            g.setColour (Paper::faint.withAlpha (0.5f));
            g.drawVerticalLine (w - 1, 0.0f, (float) h);
        }
    };

    PaperLookAndFeel& paperLnf()
    {
        static PaperLookAndFeel lnf;
        return lnf;
    }

    void paintPaper (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, int firstRuleY, int ruleSpacing)
    {
        g.setColour (Paper::paper);
        g.fillRect (r);

        g.setColour (Paper::rule.withAlpha (0.55f));
        for (int y = firstRuleY; y < r.getBottom(); y += ruleSpacing)
            g.drawHorizontalLine (y, (float) r.getX(), (float) r.getRight());

        g.setColour (Paper::margin.withAlpha (0.6f));
        g.drawVerticalLine (r.getX() + 36, (float) r.getY(), (float) r.getBottom());

        g.setColour (Paper::ink);
        g.setFont (Paper::type (20.0f, true));
        g.drawText (title, r.getX() + 52, r.getY() + 12, r.getWidth() - 70, 28, juce::Justification::centredLeft);
    }

    /** A cell / field you can type straight into. */
    struct PaperLabel : public juce::Label
    {
        PaperLabel()
        {
            setEditable (true, true, false);
            setFont (Paper::type (14.0f));
            setColour (juce::Label::textColourId, Paper::ink);
            setMinimumHorizontalScale (1.0f);
        }
    };
}

//==============================================================================
class SessionSheet::InfoPage : public juce::Component
{
public:
    explicit InfoPage (SessionSheet& o) : owner (o)
    {
        setLookAndFeel (&paperLnf());

        static const juce::StringPairArray hints = []
        {
            juce::StringPairArray h;
            h.set ("Tape Stock", "e.g. Ampex 456 / 3M 996");
            h.set ("Speed", "15 / 30 ips");
            h.set ("EQ", "NAB / CCIR / AES");
            h.set ("Alignment", "e.g. 0 VU = 185 nWb/m, -18 dBFS");
            h.set ("Noise Reduction", "None / Dolby A / dbx");
            h.set ("Tones", "e.g. 1k, 10k, 100 Hz at 0 VU on head");
            h.set ("Date", "dd mmm yyyy");
            return h;
        }();

        for (auto& key : Session::infoFields())
        {
            auto* ed = editors.add (new juce::TextEditor (key));
            ed->setFont (Paper::type (15.0f));
            ed->setTextToShowWhenEmpty (hints[key], Paper::faint.withAlpha (0.6f));
            ed->setIndents (2, 3);

            if (key == "Notes")
            {
                ed->setMultiLine (true, true);
                ed->setReturnKeyStartsNewLine (true);
                ed->setTextToShowWhenEmpty ("Session notes...", Paper::faint.withAlpha (0.6f));
                ed->setFont (Paper::type (15.0f));
                ed->setLineSpacing (1.25f);
            }

            ed->onTextChange = [this, key, ed]
            {
                if (auto* s = owner.getSession())
                {
                    s->info.set (key, ed->getText());
                    owner.markDirty();
                }
            };
            addAndMakeVisible (ed);
        }

        for (int n : { 8, 16, 24, 32, 48, 64, 72, 96 })
            trackCount.addItem (juce::String (n), n);
        trackCount.onChange = [this]
        {
            auto* s = owner.getSession();
            const int n = trackCount.getSelectedId();
            if (s == nullptr || n <= 0 || n == s->numTracks || ! owner.onTrackCountChange)
                return;

            const auto err = owner.onTrackCountChange (n);
            if (err.isNotEmpty())
            {
                trackCount.setSelectedId (s->numTracks, juce::dontSendNotification);
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tracks", err);
            }
        };
        addAndMakeVisible (trackCount);
    }

    ~InfoPage() override { setLookAndFeel (nullptr); }

    void refresh()
    {
        auto* s = owner.getSession();
        for (auto* ed : editors)
            if (! ed->hasKeyboardFocus (true))
                ed->setText (s != nullptr ? s->info[ed->getName()] : juce::String(), juce::dontSendNotification);

        if (s != nullptr)
        {
            if (trackCount.indexOfItemId (s->numTracks) < 0)
                trackCount.addItem (juce::String (s->numTracks), s->numTracks);
            trackCount.setSelectedId (s->numTracks, juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintPaper (g, getLocalBounds(), "SESSION SHEET", 10000, 30);

        g.setFont (Paper::type (12.0f, true));
        for (auto* ed : editors)
        {
            const auto b = ed->getBounds();
            g.setColour (Paper::faint);
            g.drawText (juce::String (ed->getName()).toUpperCase(), b.getX(), b.getY() - 15, b.getWidth(), 14,
                        juce::Justification::bottomLeft);

            g.setColour (Paper::rule);
            if (ed->isMultiLine())
            {
                const float lh = ed->getFont().getHeight() * 1.25f;
                for (float y = (float) b.getY() + lh + 3.0f; y < (float) b.getBottom(); y += lh)
                    g.drawHorizontalLine ((int) y, (float) b.getX(), (float) b.getRight());
            }
            else
            {
                g.drawHorizontalLine (b.getBottom() - 1, (float) b.getX(), (float) b.getRight());
            }
        }

        g.setColour (Paper::faint);
        g.drawText ("TRACKS", trackCount.getX() - 70, trackCount.getY(), 64, trackCount.getHeight(), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (52, 0).withTrimmedTop (56).withTrimmedRight (-30);
        trackCount.setBounds (getWidth() - 110, 14, 80, 26);

        const int colGap = 30, rowH = 44;
        const int colW = (r.getWidth() - colGap) / 2;
        const auto& fields = Session::infoFields();
        const int half = 8;

        for (int i = 0; i < fields.size(); ++i)
        {
            auto* ed = editors[i];
            if (fields[i] == "Notes")
            {
                const int top = r.getY() + half * rowH + 18;
                ed->setBounds (r.getX(), top, r.getWidth() - 30, juce::jmax (120, getHeight() - top - 20));
                continue;
            }

            const int col = i < half ? 0 : 1;
            const int row = i < half ? i : i - half;
            ed->setBounds (r.getX() + col * (colW + colGap), r.getY() + row * rowH + 15, colW - 30, 24);
        }
    }

private:
    SessionSheet& owner;
    juce::OwnedArray<juce::TextEditor> editors;
    juce::ComboBox trackCount;
};

//==============================================================================
class SessionSheet::TrackSheetPage : public juce::Component,
                                     private juce::TableListBoxModel
{
public:
    explicit TrackSheetPage (SessionSheet& o) : owner (o)
    {
        setLookAndFeel (&paperLnf());
        auto& h = table.getHeader();
        h.addColumn ("TRK", 1, 50, 40, 60, juce::TableHeaderComponent::notSortable);
        h.addColumn ("NAME", 2, 170, 80, 400, juce::TableHeaderComponent::notSortable);
        h.addColumn ("INSTRUMENT / SOURCE", 3, 220, 80, 500, juce::TableHeaderComponent::notSortable);
        h.addColumn ("MIC / DI", 4, 170, 60, 400, juce::TableHeaderComponent::notSortable);
        h.addColumn ("NOTES", 5, 360, 80, 2000, juce::TableHeaderComponent::notSortable);
        h.setStretchToFitActive (true);
        table.setHeaderHeight (26);
        table.setRowHeight (26);
        table.setColour (juce::ListBox::backgroundColourId, Paper::paper);
        table.setOutlineThickness (0);
        addAndMakeVisible (table);
    }

    ~TrackSheetPage() override { setLookAndFeel (nullptr); }

    void refresh() { table.updateContent(); table.repaint(); }

    void paint (juce::Graphics& g) override { paintPaper (g, getLocalBounds(), "TRACK SHEET", 10000, 26); }
    void resized() override { table.setBounds (getLocalBounds().withTrimmedTop (50).withTrimmedLeft (44).reduced (0, 0)); }

private:
    int getNumRows() override { auto* s = owner.getSession(); return s != nullptr ? s->numTracks : 0; }

    void paintRowBackground (juce::Graphics& g, int, int w, int h, bool) override
    {
        g.fillAll (Paper::paper);
        g.setColour (Paper::rule.withAlpha (0.7f));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        if (col == 1)
        {
            g.setColour (Paper::ink);
            g.setFont (Paper::type (15.0f, true));
            g.drawText (juce::String (row + 1), 0, 0, w - 8, h, juce::Justification::centredRight);
        }
        g.setColour (Paper::faint.withAlpha (0.35f));
        g.drawVerticalLine (w - 1, 0.0f, (float) h);
    }

    juce::Component* refreshComponentForCell (int row, int col, bool, juce::Component* existing) override
    {
        if (col == 1)
        {
            delete existing;
            return nullptr;
        }

        auto* label = dynamic_cast<PaperLabel*> (existing);
        if (label == nullptr)
        {
            delete existing;
            label = new PaperLabel();
        }

        auto* s = owner.getSession();
        if (s == nullptr || row >= s->numTracks)
            return label;

        auto& tr = s->tracks[(size_t) row];
        juce::String* field = col == 2 ? &tr.name : col == 3 ? &tr.instrument : col == 4 ? &tr.mic : &tr.notes;

        if (! label->isBeingEdited())
            label->setText (*field, juce::dontSendNotification);

        label->onTextChange = [this, row, col, label]
        {
            auto* s2 = owner.getSession();
            if (s2 == nullptr || row >= s2->numTracks) return;
            auto& t = s2->tracks[(size_t) row];
            auto text = label->getText();
            if (col == 2)
            {
                t.name = text.trim().isEmpty() ? "Track " + juce::String (row + 1) : text.trim();
                if (owner.onTrackNamesChanged) owner.onTrackNamesChanged();
            }
            else if (col == 3) t.instrument = text;
            else if (col == 4) t.mic = text;
            else               t.notes = text;
            owner.markDirty();
        };
        return label;
    }

    SessionSheet& owner;
    juce::TableListBox table { "Track sheet", this };
};

//==============================================================================
class SessionSheet::TakeLogPage : public juce::Component,
                                  private juce::TableListBoxModel
{
public:
    explicit TakeLogPage (SessionSheet& o) : owner (o)
    {
        setLookAndFeel (&paperLnf());
        auto& h = table.getHeader();
        h.addColumn ("TAKE", 1, 54, 40, 80, juce::TableHeaderComponent::notSortable);
        h.addColumn ("DATE / TIME", 2, 160, 80, 240, juce::TableHeaderComponent::notSortable);
        h.addColumn ("IN", 3, 96, 60, 140, juce::TableHeaderComponent::notSortable);
        h.addColumn ("OUT", 4, 96, 60, 140, juce::TableHeaderComponent::notSortable);
        h.addColumn ("TRACKS", 5, 110, 50, 300, juce::TableHeaderComponent::notSortable);
        h.addColumn ("SONG / TITLE", 6, 190, 80, 500, juce::TableHeaderComponent::notSortable);
        h.addColumn ("STATUS", 7, 140, 80, 220, juce::TableHeaderComponent::notSortable);
        h.addColumn ("COMMENTS", 8, 320, 80, 2000, juce::TableHeaderComponent::notSortable);
        h.setStretchToFitActive (true);
        table.setHeaderHeight (26);
        table.setRowHeight (28);
        table.setColour (juce::ListBox::backgroundColourId, Paper::paper);
        table.setOutlineThickness (0);
        addAndMakeVisible (table);
    }

    ~TakeLogPage() override { setLookAndFeel (nullptr); }

    void refresh()
    {
        table.updateContent();
        table.repaint();
    }

    void scrollToEnd() { table.scrollToEnsureRowIsOnscreen (juce::jmax (0, getNumRows() - 1)); }

    void paint (juce::Graphics& g) override
    {
        paintPaper (g, getLocalBounds(), "RECORDING SHEET  /  TAKE LOG", 10000, 28);
        if (getNumRows() == 0)
        {
            g.setColour (Paper::faint);
            g.setFont (Paper::type (14.0f));
            g.drawText ("Every take is logged here automatically.", 60, 90, getWidth() - 80, 24, juce::Justification::centredLeft);
        }
    }

    void resized() override { table.setBounds (getLocalBounds().withTrimmedTop (50).withTrimmedLeft (44)); }

private:
    int getNumRows() override { auto* s = owner.getSession(); return s != nullptr ? (int) s->takeLog.size() : 0; }

    void paintRowBackground (juce::Graphics& g, int row, int w, int h, bool) override
    {
        auto* s = owner.getSession();
        const bool keeper = s != nullptr && row < (int) s->takeLog.size() && s->takeLog[(size_t) row].status == "Keeper";
        g.fillAll (keeper ? juce::Colour (0xfff7e9a8) : Paper::paper);
        g.setColour (Paper::rule.withAlpha (0.7f));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);
    }

    void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
    {
        auto* s = owner.getSession();
        if (s == nullptr || row >= (int) s->takeLog.size())
            return;

        const auto& tk = s->takeLog[(size_t) row];
        juce::String text;
        switch (col)
        {
            case 1: text = juce::String (tk.take); break;
            case 2: text = tk.when; break;
            case 3: text = formatTime (tk.start, s->sampleRate).dropLastCharacters (4); break;
            case 4: text = formatTime (tk.end, s->sampleRate).dropLastCharacters (4); break;
            case 5: text = tk.tracks; break;
            default: break;
        }

        g.setColour (Paper::ink);
        g.setFont (Paper::type (14.0f, col == 1));
        g.drawText (text, 6, 0, w - 10, h, col == 1 ? juce::Justification::centred : juce::Justification::centredLeft, true);
        g.setColour (Paper::faint.withAlpha (0.35f));
        g.drawVerticalLine (w - 1, 0.0f, (float) h);
    }

    juce::Component* refreshComponentForCell (int row, int col, bool, juce::Component* existing) override
    {
        auto* s = owner.getSession();

        if (col == 7)
        {
            auto* box = dynamic_cast<juce::ComboBox*> (existing);
            if (box == nullptr)
            {
                delete existing;
                box = new juce::ComboBox();
                int id = 1;
                for (auto* st : { "", "Complete", "Keeper", "False start", "Breakdown", "Incomplete", "Overdub", "Master", "Reject" })
                    box->addItem (juce::String (st).isEmpty() ? juce::String ("-") : juce::String (st), id++);
            }

            if (s != nullptr && row < (int) s->takeLog.size())
            {
                const auto status = s->takeLog[(size_t) row].status;
                int sel = 1;
                for (int i = 0; i < box->getNumItems(); ++i)
                    if (box->getItemText (i) == status) sel = box->getItemId (i);
                box->setSelectedId (sel, juce::dontSendNotification);
            }

            box->onChange = [this, row, box]
            {
                auto* s2 = owner.getSession();
                if (s2 == nullptr || row >= (int) s2->takeLog.size()) return;
                s2->takeLog[(size_t) row].status = box->getSelectedId() <= 1 ? juce::String() : box->getText();
                owner.markDirty();
                table.repaintRow (row);
            };
            return box;
        }

        if (col != 6 && col != 8)
        {
            delete existing;
            return nullptr;
        }

        auto* label = dynamic_cast<PaperLabel*> (existing);
        if (label == nullptr)
        {
            delete existing;
            label = new PaperLabel();
        }

        if (s != nullptr && row < (int) s->takeLog.size() && ! label->isBeingEdited())
            label->setText (col == 6 ? s->takeLog[(size_t) row].song : s->takeLog[(size_t) row].comments,
                            juce::dontSendNotification);

        label->onTextChange = [this, row, col, label]
        {
            auto* s2 = owner.getSession();
            if (s2 == nullptr || row >= (int) s2->takeLog.size()) return;
            (col == 6 ? s2->takeLog[(size_t) row].song : s2->takeLog[(size_t) row].comments) = label->getText();
            owner.markDirty();
        };
        return label;
    }

    SessionSheet& owner;
    juce::TableListBox table { "Take log", this };
};

//==============================================================================
SessionSheet::SessionSheet (std::function<Session*()> gs) : getSession (std::move (gs))
{
    infoPage  = new InfoPage (*this);
    trackPage = new TrackSheetPage (*this);
    takePage  = new TakeLogPage (*this);

    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    tabs.addTab ("SESSION", Paper::paperDark, infoPage, true);
    tabs.addTab ("TRACK SHEET", Paper::paperDark, trackPage, true);
    tabs.addTab ("TAKE LOG", Paper::paperDark, takePage, true);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, Paper::paper);
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId, Paper::ink);
    tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId, Paper::ink);
    addAndMakeVisible (tabs);

    refresh();
}

SessionSheet::~SessionSheet()
{
    if (isTimerRunning())
        timerCallback();   // don't lose the last edit
}

void SessionSheet::refresh()
{
    infoPage->refresh();
    trackPage->refresh();
    takePage->refresh();
}

void SessionSheet::showTakeLog()
{
    tabs.setCurrentTabIndex (2);
    takePage->refresh();
    takePage->scrollToEnd();
}

void SessionSheet::markDirty()
{
    startTimer (700);   // save shortly after typing stops
}

void SessionSheet::timerCallback()
{
    stopTimer();
    if (onSave) onSave();
}

void SessionSheet::paint (juce::Graphics& g) { g.fillAll (Colours::panel); }
void SessionSheet::resized() { tabs.setBounds (getLocalBounds()); }

//==============================================================================
SessionSheetWindow::SessionSheetWindow (std::function<Session*()> gs)
    : juce::DocumentWindow ("Hawthorne - Session Sheet", Colours::panel, juce::DocumentWindow::closeButton),
      sheet (std::move (gs))
{
    setUsingNativeTitleBar (true);
    setContentNonOwned (&sheet, false);
    setResizable (true, false);
    setResizeLimits (700, 500, 4000, 3000);
    centreWithSize (1180, 820);
}

} // namespace hawthorne
