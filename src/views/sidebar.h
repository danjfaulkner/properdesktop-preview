// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/signal.h"
#include "ui/view.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace properdesktop::pdf {
struct Destination;
struct OutlineItem;
}

namespace properdesktop::ui {
class ListView;
}

class PdfDocument;

// Thumbnails of the window's items down its left edge, in the material and
// selection style of the Files sidebar; the current one is selected and
// kept in view. For a PDF the items are its pages, numbered, and a document
// with an outline gets Thumbnails and Contents at the top.
class Sidebar : public properdesktop::ui::View
{
public:
    // The Files sidebar's range, which a divider on its edge resizes within.
    static constexpr double kDefaultWidth = 190;
    static constexpr double kMinimumWidth = 140;
    static constexpr double kMaximumWidth = 400;

    // The header's height, which the thumbnails start below.
    explicit Sidebar(double headerHeight);
    ~Sidebar() override;

    void setItems(const std::vector<std::string>& paths);
    // Its pages instead of files; null goes back to files. It outlives the
    // sidebar's use of it.
    void setDocument(PdfDocument* document);
    void setCurrent(int index);
    // Thumbnails read again, of files written since.
    void refresh();
    // While its width is being dragged, page thumbnails draw what they have,
    // scaled; at the end they are rendered at their new size.
    void setResizing(bool resizing);
    int current() const { return m_current; }
    size_t count() const;

    bool hasContents() const;
    bool showsContents() const { return m_showsContents; }
    void setShowsContents(bool contents);
    // The outline's rows as they are shown, an item's children under it
    // while it is open.
    size_t contentsRowCount() const { return m_outlineRows.size(); }
    const std::string& contentsTitle(size_t row) const;
    void toggleContentsRow(size_t row);
    void activateContentsRow(size_t row);
    // The thumbnail and the number of a page row in sight, in the window;
    // empty when the row is not.
    properdesktop::ui::RectF pageThumbnailGeometry(int page, double ratio) const;
    properdesktop::ui::RectF pageNumberGeometry(int page) const;
    properdesktop::ui::RectF rowGeometry(int page) const;

    std::function<void(int index)> onActivated;
    std::function<void(const properdesktop::pdf::Destination& destination)> onDestinationActivated;

protected:
    void layout() override;
    void paint(properdesktop::ui::Painter& painter) override;

private:
    class Row;
    class OutlineRow;

    struct OutlineEntry
    {
        const properdesktop::pdf::OutlineItem* item;
        int depth;
    };

    void outlineChanged();
    void flattenOutline();
    void keepVisibleThumbnails();
    // A document's rows follow its pages' shape and the window's scale.
    void updateRowHeight();
    // A page number's height, whole pixels.
    double numberHeight(double ratio);

    double m_headerHeight;
    properdesktop::ui::ListView* m_list;
    properdesktop::ui::ListView* m_outline;
    std::vector<std::string> m_paths;
    PdfDocument* m_document = nullptr;
    std::vector<properdesktop::base::Subscription> m_subscriptions;
    std::vector<OutlineEntry> m_outlineRows;
    std::set<const properdesktop::pdf::OutlineItem*> m_open;
    bool m_showsContents = false;
    int m_current = -1;
    // A page thumbnail's box, for the document's rows: the width less the
    // plate's and the page's insets, and the height of the page tallest for
    // its width, at most one and a half widths.
    double m_pageWidth = 0;
    double m_pageHeight = 0;
    double m_pageAspect = 0;
    bool m_resizing = false;
    double m_numberHeight = 0;
    double m_numberRatio = 0;
    std::string m_numberFont;
};
