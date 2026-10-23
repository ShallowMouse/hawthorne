#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VintageLookAndFeel.h"
#include "Session.h"
#include "NewSessionDialog.h"
#include "Settings.h"

//==============================================================================
// ProjectManagerDialog
//
// Layout:
//   Header  (title + subtitle)
//   ─────────────────────────────────────────────────────────────
//   [  PROJECTS          ] | [  SESSIONS                      ]
//   [ ─── project list ─ ] | [ ──── session list ────────── ]
//   [ New Project  Delete] | [ New Session  Delete           ]
//   ─────────────────────────────────────────────────────────────
//   Default folder:  [path display     ] [Browse]
//   ─────────────────────────────────────────────────────────────
//   [                              Exit ]
//
// File structure on disk:
//   <rootFolder>/
//     <ProjectName>/          ← project = top-level directory
//       <SessionName>/        ← session = direct child directory
//         session.xml
//         Audio/
//==============================================================================
class ProjectManagerDialog : public juce::Component
{
public:
    enum class Result { None, NewSession, OpenSession, Cancel };

    ProjectManagerDialog(Settings* settingsPtr = nullptr)
        : appSettings(settingsPtr),
          projectListModel(*this),
          sessionListModel(*this)
    {
        setSize(1000, 660);

        // ---- Header ----
        titleLabel.setText("VTape24 Project Manager", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff8f0e0));
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);

        subtitleLabel.setText("Professional Multitrack Recording", juce::dontSendNotification);
        subtitleLabel.setFont(juce::Font(12.0f));
        subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc4b49a));
        subtitleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(subtitleLabel);

        // ---- Column headers ----
        for (auto* lbl : { &projectsColHeader, &sessionsColHeader })
        {
            lbl->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
            lbl->setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
            addAndMakeVisible(lbl);
        }
        projectsColHeader.setText("PROJECTS", juce::dontSendNotification);
        sessionsColHeader.setText("SESSIONS", juce::dontSendNotification);

        // ---- Project list ----
        projectList.setModel(&projectListModel);
        projectList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff16130f));
        projectList.setColour(juce::ListBox::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        projectList.setRowHeight(44);
        addAndMakeVisible(projectList);

        // ---- Session list ----
        sessionList.setModel(&sessionListModel);
        sessionList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff16130f));
        sessionList.setColour(juce::ListBox::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        sessionList.setRowHeight(56);
        addAndMakeVisible(sessionList);

        // ---- Project buttons ----
        styleBtn(newProjectButton, "＋ New Project");
        newProjectButton.onClick = [this] { promptNewProject(); };
        addAndMakeVisible(newProjectButton);

        styleBtn(deleteProjectButton, "✕ Delete");
        deleteProjectButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcc4444));
        deleteProjectButton.onClick = [this] { deleteSelectedProject(); };
        addAndMakeVisible(deleteProjectButton);

        // ---- Session buttons ----
        styleBtn(newSessionButton, "＋ New Session");
        newSessionButton.onClick = [this] { showNewSessionDialog(); };
        addAndMakeVisible(newSessionButton);

        styleBtn(openSessionButton, "▶ Open");
        openSessionButton.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentGreen));
        openSessionButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(VintageLookAndFeel::accentGreen));
        openSessionButton.onClick = [this] { openSelectedSession(); };
        addAndMakeVisible(openSessionButton);

        styleBtn(deleteSessionButton, "✕ Delete");
        deleteSessionButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcc4444));
        deleteSessionButton.onClick = [this] { deleteSelectedSession(); };
        addAndMakeVisible(deleteSessionButton);

        // ---- Default folder row ----
        defaultPathLabel.setText("Root Folder:", juce::dontSendNotification);
        defaultPathLabel.setFont(juce::Font(11.0f));
        defaultPathLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc4b49a));
        addAndMakeVisible(defaultPathLabel);

        defaultPathDisplay.setEditable(false);
        defaultPathDisplay.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1512));
        defaultPathDisplay.setColour(juce::Label::textColourId, juce::Colour(0xffe8dcc8));
        defaultPathDisplay.setColour(juce::Label::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        defaultPathDisplay.setBorderSize(juce::BorderSize<int>(3));
        defaultPathDisplay.setText(getRootFolder().getFullPathName(), juce::dontSendNotification);
        addAndMakeVisible(defaultPathDisplay);

        styleBtn(browsePathButton, "Browse…");
        browsePathButton.onClick = [this] { browseForRoot(); };
        addAndMakeVisible(browsePathButton);

        // ---- Exit ----
        styleBtn(cancelButton, "Exit");
        cancelButton.onClick = [this] { result = Result::Cancel; closeDialog(); };
        addAndMakeVisible(cancelButton);

        refreshProjects();
    }

    // ---- Public result API ----
    Result getResult() const { return result; }
    juce::File getSelectedSessionFile() const { return selectedSessionFile; }
    std::function<void()> onDialogClosed;

    // Called by session list model to tell the dialog a row was double-clicked
    void sessionDoubleClicked(int row)
    {
        if (row >= 0 && row < currentSessions.size())
        {
            selectedSessionFile = currentSessions[row].getChildFile("session.xml");
            result = Result::OpenSession;
            addToRecentSessions(selectedSessionFile);
            closeDialog();
        }
    }

    // Called by project list model when selection changes
    void projectSelectionChanged(int row)
    {
        selectedProjectIndex = row;
        refreshSessions();
        updateButtonStates();
    }

    // Called by session list model when selection changes
    void sessionSelectionChanged(int /*row*/)
    {
        updateButtonStates();
    }

    // ---- Paint ----
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds();
        juce::ColourGradient bg(juce::Colour(VintageLookAndFeel::bgTrack), 0.0f, 0.0f,
                                juce::Colour(0xff0a0a08), 0.0f, (float)b.getHeight(), false);
        g.setGradientFill(bg);
        g.fillAll();

        // Outer border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRect(b, 2);

        // Header band
        auto header = b.removeFromTop(80);
        juce::ColourGradient hg(juce::Colour(VintageLookAndFeel::tapeBeige), 0.0f, 0.0f,
                                juce::Colour(0xffc4b49a), 0.0f, (float)header.getHeight(), false);
        g.setGradientFill(hg);
        g.fillRect(header);
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawLine(0.0f, 80.0f, (float)getWidth(), 80.0f, 2.0f);

        // Vertical divider between columns
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawLine((float)getWidth() / 2.0f, 80.0f, (float)getWidth() / 2.0f, (float)getHeight() - 80.0f, 1.5f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced(16, 12);

        // Header
        auto header = b.removeFromTop(80 - 12);
        titleLabel.setBounds(header.removeFromTop(40));
        subtitleLabel.setBounds(header.removeFromTop(20));
        b.removeFromTop(8);

        // Footer strip
        auto footer = b.removeFromBottom(40);
        {
            auto pathRow = footer.removeFromLeft(footer.getWidth() - 120);
            defaultPathLabel.setBounds(pathRow.removeFromLeft(82));
            browsePathButton.setBounds(pathRow.removeFromRight(72));
            pathRow.removeFromLeft(4);
            defaultPathDisplay.setBounds(pathRow);
            footer.removeFromLeft(8);
            cancelButton.setBounds(footer);
        }
        b.removeFromBottom(8);

        // Button rows above footer
        auto btnRow = b.removeFromBottom(36);
        b.removeFromBottom(4);

        int half = b.getWidth() / 2 - 8;

        // Project button row (left half)
        {
            auto lr = btnRow.removeFromLeft(half);
            newProjectButton.setBounds(lr.removeFromLeft(140));
            lr.removeFromLeft(6);
            deleteProjectButton.setBounds(lr.removeFromLeft(80));
        }
        btnRow.removeFromLeft(16);

        // Session button row (right half)
        {
            auto rr = btnRow;
            openSessionButton.setBounds(rr.removeFromLeft(100));
            rr.removeFromLeft(6);
            newSessionButton.setBounds(rr.removeFromLeft(140));
            rr.removeFromLeft(6);
            deleteSessionButton.setBounds(rr.removeFromLeft(80));
        }

        // Column headers
        auto colHeaders = b.removeFromTop(24);
        projectsColHeader.setBounds(colHeaders.removeFromLeft(half).reduced(4, 0));
        colHeaders.removeFromLeft(16);
        sessionsColHeader.setBounds(colHeaders.reduced(4, 0));
        b.removeFromTop(4);

        // Lists
        projectList.setBounds(b.removeFromLeft(half));
        b.removeFromLeft(16);
        sessionList.setBounds(b);
    }

    // ---- List models ----
    struct ProjectListModel : public juce::ListBoxModel
    {
        ProjectManagerDialog& owner;
        explicit ProjectListModel(ProjectManagerDialog& o) : owner(o) {}

        int getNumRows() override { return owner.projects.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            auto b = juce::Rectangle<int>(0, 0, w, h);
            if (selected)
                g.setColour(juce::Colour(VintageLookAndFeel::accentAmber).withAlpha(0.25f));
            else
                g.setColour(row % 2 == 0 ? juce::Colour(0xff1e1a14) : juce::Colour(0xff181410));
            g.fillRect(b);

            if (row < owner.projects.size())
            {
                g.setColour(selected ? juce::Colour(VintageLookAndFeel::accentAmber) : juce::Colour(0xfff0e8d8));
                g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
                g.drawText("📁  " + owner.projects[row].getFileName(),
                           b.reduced(10, 0), juce::Justification::centredLeft);
            }
        }

        void selectedRowsChanged(int lastRow) override { owner.projectSelectionChanged(lastRow); }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { owner.projectSelectionChanged(row); }
    };

    struct SessionListModel : public juce::ListBoxModel
    {
        ProjectManagerDialog& owner;
        explicit SessionListModel(ProjectManagerDialog& o) : owner(o) {}

        int getNumRows() override { return owner.currentSessions.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            auto b = juce::Rectangle<int>(0, 0, w, h);
            if (selected)
                g.setColour(juce::Colour(VintageLookAndFeel::accentAmber).withAlpha(0.25f));
            else
                g.setColour(row % 2 == 0 ? juce::Colour(0xff1e1a14) : juce::Colour(0xff181410));
            g.fillRect(b);

            if (row >= owner.currentSessions.size()) return;

            const auto& sessionDir = owner.currentSessions[row];
            juce::File xmlFile = sessionDir.getChildFile("session.xml");

            g.setColour(selected ? juce::Colour(VintageLookAndFeel::accentAmber) : juce::Colour(0xfff0e8d8));
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold));
            g.drawText(sessionDir.getFileName(), b.reduced(10, 5).removeFromTop(22), juce::Justification::centredLeft);

            // Load and show brief session details
            Session tempSession;
            if (xmlFile.existsAsFile() && tempSession.loadFromFile(xmlFile))
            {
                g.setColour(juce::Colour(0xff9a9080));
                g.setFont(juce::Font(10.0f));
                juce::String details = juce::String(tempSession.getSampleRate()) + " Hz  · "
                                     + juce::String(tempSession.getBitDepth()) + "-bit  · "
                                     + juce::String(tempSession.getTrackCount()) + " tracks  · "
                                     + juce::String(tempSession.getBPM(), 1) + " BPM";
                g.drawText(details, b.reduced(10, 5).removeFromBottom(20), juce::Justification::centredLeft);
            }

            // Row separator
            g.setColour(juce::Colour(0xff2a2620));
            g.drawHorizontalLine(h - 1, 0.0f, (float)w);
        }

        void selectedRowsChanged(int lastRow) override { owner.sessionSelectionChanged(lastRow); }
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { owner.sessionDoubleClicked(row); }
    };

private:
    // ---- Data scanning ----
    juce::File getRootFolder() const
    {
        if (appSettings)
        {
            juce::String p = appSettings->getDefaultSavePath();
            if (p.isNotEmpty()) return juce::File(p);
        }
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("VTape24").getChildFile("Projects");
    }

    void refreshProjects()
    {
        projects.clear();
        juce::File root = getRootFolder();
        if (root.isDirectory())
            root.findChildFiles(projects, juce::File::findDirectories, false);
        projects.sort();
        projectList.updateContent();
        projectList.deselectAllRows();
        selectedProjectIndex = -1;
        refreshSessions();
        updateButtonStates();
    }

    void refreshSessions()
    {
        currentSessions.clear();
        if (selectedProjectIndex >= 0 && selectedProjectIndex < projects.size())
        {
            juce::Array<juce::File> dirs;
            projects[selectedProjectIndex].findChildFiles(dirs, juce::File::findDirectories, false);
            dirs.sort();
            for (auto& d : dirs)
                if (d.getChildFile("session.xml").existsAsFile())
                    currentSessions.add(d);
        }
        sessionList.updateContent();
        sessionList.deselectAllRows();
        updateButtonStates();
    }

    void updateButtonStates()
    {
        bool hasProject = selectedProjectIndex >= 0 && selectedProjectIndex < projects.size();
        bool hasSession = sessionList.getSelectedRow() >= 0;

        newSessionButton.setEnabled(hasProject);
        openSessionButton.setEnabled(hasSession);
        deleteProjectButton.setEnabled(hasProject);
        deleteSessionButton.setEnabled(hasSession);
    }

    // ---- Actions ----
    void promptNewProject()
    {
        auto* editor = new juce::AlertWindow("New Project", "Enter project name:", juce::MessageBoxIconType::NoIcon);
        editor->addTextEditor("name", "My Project");
        editor->addButton("Create", 1, juce::KeyPress(juce::KeyPress::returnKey));
        editor->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        editor->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, editor](int code)
            {
                if (code == 1)
                {
                    juce::String name = editor->getTextEditorContents("name").trim();
                    if (name.isNotEmpty())
                    {
                        juce::File dir = getRootFolder().getChildFile(name);
                        dir.createDirectory();
                        refreshProjects();
                        // Auto-select the new project
                        for (int i = 0; i < projects.size(); ++i)
                            if (projects[i] == dir)
                            {
                                projectList.selectRow(i);
                                break;
                            }
                    }
                }
                delete editor;
            }), true);
    }

    void deleteSelectedProject()
    {
        if (selectedProjectIndex < 0 || selectedProjectIndex >= projects.size()) return;
        auto dir = projects[selectedProjectIndex];
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon,
            "Delete Project?",
            "Permanently delete project \"" + dir.getFileName() + "\" and all its sessions?\nThis cannot be undone.",
            "Delete", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this, dir](int code) mutable
            {
                if (code == 1)
                {
                    dir.deleteRecursively();
                    refreshProjects();
                }
            }));
    }

    void showNewSessionDialog()
    {
        if (selectedProjectIndex < 0 || selectedProjectIndex >= projects.size()) return;

        newSessionDialog = std::make_unique<NewSessionDialog>();
        juce::File projectDir = projects[selectedProjectIndex];

        newSessionDialog->onDialogClosed = [this, projectDir]()
        {
            if (!newSessionDialog->wasCancelled())
            {
                auto session = newSessionDialog->createSession(projectDir);
                if (session)
                {
                    selectedSessionFile = session->getSessionFilePath();
                    result = Result::NewSession;
                    addToRecentSessions(selectedSessionFile);
                    if (newSessionDialogWindow)
                    {
                        newSessionDialogWindow->exitModalState(0);
                        newSessionDialogWindow = nullptr;
                    }
                    newSessionDialog = nullptr;
                    closeDialog();
                }
            }
            else
            {
                if (newSessionDialogWindow) { newSessionDialogWindow->exitModalState(0); newSessionDialogWindow = nullptr; }
                newSessionDialog = nullptr;
            }
        };

        newSessionDialogWindow = std::make_unique<juce::DocumentWindow>(
            "New Session — " + projectDir.getFileName(),
            juce::Colour(VintageLookAndFeel::bgTrack),
            juce::DocumentWindow::closeButton);
        newSessionDialogWindow->setContentNonOwned(newSessionDialog.get(), true);
        newSessionDialogWindow->centreWithSize(600, 650);
        newSessionDialogWindow->setUsingNativeTitleBar(true);
        newSessionDialogWindow->setVisible(true);
        newSessionDialogWindow->enterModalState(true);
    }

    void openSelectedSession()
    {
        int row = sessionList.getSelectedRow();
        sessionDoubleClicked(row);
    }

    void deleteSelectedSession()
    {
        int row = sessionList.getSelectedRow();
        if (row < 0 || row >= currentSessions.size()) return;
        auto dir = currentSessions[row];
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon,
            "Delete Session?",
            "Permanently delete session \"" + dir.getFileName() + "\"?\nThis cannot be undone.",
            "Delete", "Cancel", nullptr,
            juce::ModalCallbackFunction::create([this, dir](int code) mutable
            {
                if (code == 1)
                {
                    dir.deleteRecursively();
                    refreshSessions();
                }
            }));
    }

    void browseForRoot()
    {
        juce::File start = getRootFolder();
        pathChooser = std::make_unique<juce::FileChooser>("Choose Root Projects Folder", start, "", true);
        pathChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& c)
            {
                auto f = c.getResult();
                if (f != juce::File() && appSettings)
                {
                    appSettings->setDefaultSavePath(f.getFullPathName());
                    defaultPathDisplay.setText(f.getFullPathName(), juce::dontSendNotification);
                    refreshProjects();
                }
            });
    }

    void closeDialog() { if (onDialogClosed) onDialogClosed(); }

    // ---- Recent sessions (reused by AudioEngine) ----
    void addToRecentSessions(const juce::File& sessionFile)
    {
        auto propsFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("VTape24").getChildFile("recent_sessions.xml");
        juce::Array<juce::File> recents;
        if (propsFile.existsAsFile())
        {
            if (auto xml = std::unique_ptr<juce::XmlElement>(juce::XmlDocument::parse(propsFile)))
                if (auto* r = xml->getChildByName("RecentSessions"))
                    for (auto* ch : r->getChildIterator())
                    { juce::File f(ch->getAllSubText()); if (f.existsAsFile()) recents.add(f); }
        }
        recents.removeAllInstancesOf(sessionFile);
        recents.insert(0, sessionFile);
        while (recents.size() > 10) recents.removeLast();
        juce::XmlElement root("VTape24Settings");
        auto* r = root.createNewChildElement("RecentSessions");
        for (auto& s : recents) { auto* ch = r->createNewChildElement("Session"); ch->addTextElement(s.getFullPathName()); }
        propsFile.getParentDirectory().createDirectory();
        root.writeTo(propsFile);
    }

    // ---- Helpers ----
    void styleBtn(juce::TextButton& b, const juce::String& text)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2a2520));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        b.setColour(juce::TextButton::textColourOffId,  juce::Colour(VintageLookAndFeel::accentAmber));
        b.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xff1a1510));
    }

    // ---- Members ----
    Settings* appSettings = nullptr;

    juce::Label titleLabel, subtitleLabel;
    juce::Label projectsColHeader, sessionsColHeader;

    ProjectListModel projectListModel;
    SessionListModel sessionListModel;
    juce::ListBox    projectList, sessionList;

    juce::TextButton newProjectButton, deleteProjectButton;
    juce::TextButton newSessionButton, openSessionButton, deleteSessionButton;

    juce::Label      defaultPathLabel;
    juce::Label      defaultPathDisplay;
    juce::TextButton browsePathButton;
    juce::TextButton cancelButton;

    juce::Array<juce::File> projects;
    juce::Array<juce::File> currentSessions;
    int selectedProjectIndex = -1;

    Result result = Result::None;
    juce::File selectedSessionFile;

    std::unique_ptr<NewSessionDialog>      newSessionDialog;
    std::unique_ptr<juce::DocumentWindow>  newSessionDialogWindow;
    std::unique_ptr<juce::FileChooser>     pathChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManagerDialog)
};
