//
// Terminal / Components / TerminalTable.hpp
// Table：普通表格 + 单元格编辑 + 行筛选 + CSV / SpreadsheetML 导入导出。
// 单元格文本直接使用 TStrFmt 语法，渲染时解析成「文字 + Style」；
// 不改变 ConsoleBackBuffer 的存储结构。
//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../Core/UI.hpp"
#include "../Core/TerminalRichText.hpp"
#include "../Core/TerminalKeyFocus.hpp"
#include "../Utils/BoxChars.hpp"
#include "../Utils/Theme.hpp"

namespace Terminal {

    class Table : public UI {
    public:
        struct Column {
            std::string title{};
            int width{ 12 };
            LayoutPos align{ LayoutPos::Left };
            bool editable{ true };
        };

        bool headerRow{ true };
        bool striped{ false };
        bool showGrid{ true };
        bool editable{ false };

        // 单元格修改回调（row/col 是真实数据下标，不受筛选影响）
        std::function<void(std::size_t, std::size_t, const std::string&)> onCellChanged{};

        Table() {
            signature = "UITable";
            const Theme& t = Theme::Current();
            style = Style(t.text, t.surface);
            gridStyle = Style(t.border, t.surface);
            headerStyle = Style(t.accent, t.surface).SetBold();
            selectionStyle = Style(t.accent, t.surface).SetBold();
            editStyle = Style(t.warning, t.surface).SetUnderline();
        }

        //---------- 列 / 行 ----------
        void AddColumn(const std::string& title, const int width = 12, const LayoutPos align = LayoutPos::Left) {
            m_columns.push_back(Column{ title, std::max(width, 3), align, true });
            for (auto& row : m_rows) row.resize(m_columns.size());
            InvalidateLayout();
        }

        void SetColumns(const std::vector<Column>& columns) {
            m_columns = columns;
            for (auto& c : m_columns) c.width = std::max(c.width, 3);
            for (auto& row : m_rows) row.resize(m_columns.size());
            m_columns.empty() ? ClearSelection() : ClampSelection();
            InvalidateLayout();
        }

        void SetColumns(const std::vector<std::string>& titles, const int defaultWidth = 12) {
            std::vector<Column> cols;
            cols.reserve(titles.size());
            for (const auto& t : titles) cols.push_back(Column{ t, std::max(defaultWidth, 3), LayoutPos::Left, true });
            SetColumns(cols);
        }

        [[nodiscard]] std::size_t ColumnCount() const noexcept { return m_columns.size(); }
        [[nodiscard]] std::size_t RowCount() const noexcept { return m_rows.size(); }
        [[nodiscard]] const std::vector<Column>& Columns() const noexcept { return m_columns; }
        [[nodiscard]] const std::vector<std::vector<std::string>>& Rows() const noexcept { return m_rows; }

        void AddRow(const std::vector<std::string>& cells) {
            std::vector<std::string> row(cells.begin(), cells.end());
            row.resize(m_columns.size());
            m_rows.push_back(std::move(row));
            InvalidateLayout();
        }

        void SetRows(const std::vector<std::vector<std::string>>& rows) {
            m_rows = rows;
            for (auto& row : m_rows) row.resize(m_columns.size());
            ClampSelection();
            InvalidateLayout();
        }

        void RemoveRow(const std::size_t row) {
            if (row >= m_rows.size()) return;
            m_rows.erase(m_rows.begin() + static_cast<std::ptrdiff_t>(row));
            m_filterDirty = true;
            ClampSelection();
            InvalidateLayout();
            Invalidate();
        }

        [[nodiscard]] std::string GetCell(const std::size_t row, const std::size_t col) const {
            if (row >= m_rows.size() || col >= m_columns.size()) return {};
            return m_rows[row][col];
        }

        void SetCell(const std::size_t row, const std::size_t col, std::string text) {
            if (row >= m_rows.size() || col >= m_columns.size()) return;
            m_rows[row][col] = std::move(text);
            m_filterDirty = true;
            InvalidateLayout();
            Invalidate();
        }

        //---------- 编辑 ----------
        void SetEditable(const bool on) {
            editable = on;
            focusable = on;
            if (!on) CancelEdit();
            Invalidate();
        }

        [[nodiscard]] bool IsEditable() const noexcept { return editable; }
        [[nodiscard]] bool IsEditing() const noexcept { return m_editing; }

        [[nodiscard]] int SelectedRow() const noexcept { return m_selRow; }
        [[nodiscard]] int SelectedColumn() const noexcept { return m_selCol; }

        // 当前单元格在真实行中的位置（筛选后仍返回真实下标）
        void SelectCell(const int row, const int col) {
            if (m_rows.empty() || m_columns.empty()) { ClearSelection(); return; }
            m_selRow = std::clamp(row, 0, static_cast<int>(m_rows.size()) - 1);
            m_selCol = std::clamp(col, 0, static_cast<int>(m_columns.size()) - 1);
            Invalidate();
        }

        //---------- 筛选 ----------
        // col 为真实列下标；query 对 TStrFmt 解析后的可见文字做不区分大小写匹配。
        void SetFilter(const int col, std::string query) {
            m_filterColumn = (col >= 0 && col < static_cast<int>(m_columns.size())) ? col : -1;
            m_filterText = std::move(query);
            m_filterDirty = true;
            if (m_filterColumn < 0 || m_filterText.empty()) {
                m_filterColumn = -1;
                m_filterText.clear();
            }
            ClampSelection();
            InvalidateLayout();
            Invalidate();
        }

        void ClearFilter() {
            m_filterColumn = -1;
            m_filterText.clear();
            m_filterDirty = true;
            ClampSelection();
            InvalidateLayout();
            Invalidate();
        }

        [[nodiscard]] bool HasFilter() const noexcept { return m_filterColumn >= 0 && !m_filterText.empty(); }
        [[nodiscard]] int FilterColumn() const noexcept { return m_filterColumn; }
        [[nodiscard]] const std::string& FilterText() const noexcept { return m_filterText; }

        // 筛选后的真实行下标。
        [[nodiscard]] std::vector<std::size_t> VisibleRows() const {
            BuildVisible();
            return m_visible;
        }

        [[nodiscard]] std::size_t FilteredRowCount() const {
            BuildVisible();
            return m_visible.size();
        }

        //---------- 导入 / 导出 ----------
        // Excel 可直接打开 CSV；SpreadsheetML（.xml）也能被 Excel 直接打开。
        // 本库不引入 ZIP/deflate 依赖，因此不伪装成 .xlsx。
        bool ImportCsv(const std::string& path, const bool firstRowHeader = true);
        bool ExportCsv(const std::string& path) const;
        bool ImportExcel(const std::string& path);
        bool ExportExcel(const std::string& path) const;

        // 自动按内容调整列宽；maxWidth 限制 CJK/长文本导致的过宽。
        void AutoFitColumns(const int maxWidth = 30);

        //---------- UI ----------
        [[nodiscard]] int MeasureWidth() const override {
            return layout.width >= 0 ? layout.width : TableWidth();
        }

        [[nodiscard]] int MeasureHeight() const override {
            const auto visible = VisibleRows().size();
            return static_cast<int>(visible) + (headerRow ? 4 : 2);
        }

        [[nodiscard]] MouseCursor CursorAt(const int x, const int y) const override {
            if (!isVisible) return MouseCursor::Arrow;
            if (editable && CellAt(x, y).valid) return MouseCursor::IBeam;
            return UI::CursorAt(x, y);
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            if (m_columns.empty() || bounds.IsEmpty()) return;

            const auto visible = VisibleRows();
            DrawFrame(buf, static_cast<int>(visible.size()));
        }

        bool OnMouse(const xMouse& ev) override {
            if (!isVisible || !isEnabled) return false;
            if (ev.action != MouseAction::Down && ev.action != MouseAction::DoubleClick) return false;

            const CellHit hit = CellAt(ev.x, ev.y);
            if (!hit.valid) return false;

            SelectCell(static_cast<int>(hit.row), static_cast<int>(hit.col));
            if (editable && hit.col < m_columns.size() && m_columns[hit.col].editable &&
                (ev.action == MouseAction::DoubleClick || ev.action == MouseAction::Down)) {
                if (ev.action == MouseAction::DoubleClick) BeginEdit(hit.row, hit.col);
            }
            Invalidate();
            return true;
        }

        bool OnKey(const xChar& key) override {
            if (!isVisible || !isEnabled || !editable) return false;

            if (key == SKey::Esc) {
                if (m_editing) { CancelEdit(); return true; }
                return false;
            }
            if (key == SKey::Enter) {
                if (m_editing) CommitEdit();
                else BeginEditSelected();
                return true;
            }
            if (key == SKey::Tab) {
                if (m_editing) CommitEdit();
                MoveSelection(m_selCol + 1 < static_cast<int>(m_columns.size()) ? m_selCol + 1 : 0,
                    m_selCol + 1 < static_cast<int>(m_columns.size()) ? m_selRow : m_selRow + 1);
                return true;
            }
            if (key == SKey::UpArrow || key == SKey::DownArrow ||
                key == SKey::LeftArrow || key == SKey::RightArrow) {
                if (m_editing) CommitEdit();
                MoveSelection(m_selCol + (key == SKey::RightArrow ? 1 : key == SKey::LeftArrow ? -1 : 0),
                    m_selRow + (key == SKey::DownArrow ? 1 : key == SKey::UpArrow ? -1 : 0));
                return true;
            }
            if (key == SKey::BackSpace || key == SKey::FastBackSpace) {
                if (!m_editing) BeginEditSelected();
                if (m_editing) {
                    auto cps = Utf8::Decode(m_editBuffer);
                    if (!cps.empty()) cps.pop_back();
                    m_editBuffer.clear();
                    for (const auto cp : cps) m_editBuffer += Utf8::Encode(cp);
                    Invalidate();
                }
                return true;
            }

            if (key.IsPrintableAscii() || key.IsUtf8Multibyte()) {
                if (!m_editing) BeginEditSelected();
                if (!m_editing) return false;
                if (key == SKey::Space) m_editBuffer.push_back(' ');
                else m_editBuffer += key.ToString();
                Invalidate();
                return true;
            }
            return false;
        }

        void OnBlur() override {
            if (m_editing) CommitEdit();
        }

        void OnFocus() override { Invalidate(); }

        // 可直接读取单元格的纯显示文字（用于筛选/导出预览）
        [[nodiscard]] std::string PlainCell(const std::size_t row, const std::size_t col) const {
            if (row >= m_rows.size() || col >= m_columns.size()) return {};
            return TStrFmt::Parse(m_rows[row][col], style).Plain();
        }

    protected:
        struct CellHit {
            bool valid{ false };
            std::size_t row{ 0 };
            std::size_t col{ 0 };
        };

        // 当前表格实际占用的宽度（不依赖 bounds）
        [[nodiscard]] int TableWidth() const {
            if (m_columns.empty()) return 0;
            int w = 2;
            for (const auto& c : m_columns) w += std::max(c.width, 1);
            w += static_cast<int>(m_columns.size()) - 1;
            return w;
        }

        [[nodiscard]] int DataTop() const { return Top() + (headerRow ? 3 : 1); }

        [[nodiscard]] int ColumnX(const int col) const {
            int x = Left() + 1;
            for (int i = 0; i < col; ++i) x += std::max(m_columns[static_cast<std::size_t>(i)].width, 1) + 1;
            return x;
        }

        [[nodiscard]] int ColumnWidth(const int col) const {
            if (col < 0 || col >= static_cast<int>(m_columns.size())) return 0;
            return std::max(m_columns[static_cast<std::size_t>(col)].width, 1);
        }

        [[nodiscard]] CellHit CellAt(const int x, const int y) const {
            if (!bounds.Contains(Position{ x, y }) || m_columns.empty()) return {};
            const int dataTop = DataTop();
            if (y < dataTop) return {};

            const auto visible = VisibleRows();
            const int vi = y - dataTop;
            if (vi < 0 || vi >= static_cast<int>(visible.size())) return {};
            if (x == Left() || x >= Right()) return {};

            int cx = Left() + 1;
            for (int c = 0; c < static_cast<int>(m_columns.size()); ++c) {
                const int w = ColumnWidth(c);
                if (x >= cx && x < cx + w) return { true, visible[static_cast<std::size_t>(vi)], static_cast<std::size_t>(c) };
                cx += w + 1;   // 跳过竖分隔符
            }
            return {};
        }

        [[nodiscard]] bool CellEditable(const std::size_t col) const {
            return editable && col < m_columns.size() && m_columns[col].editable;
        }

        void EnsureSelection() {
            if (m_rows.empty() || m_columns.empty()) { ClearSelection(); return; }
            m_selRow = std::clamp(m_selRow, 0, static_cast<int>(m_rows.size()) - 1);
            m_selCol = std::clamp(m_selCol, 0, static_cast<int>(m_columns.size()) - 1);
        }

        void ClearSelection() {
            m_selRow = -1;
            m_selCol = -1;
            m_editing = false;
            m_editRow = m_editCol = -1;
        }

        void ClampSelection() {
            EnsureSelection();
            if (HasFilter()) {
                const auto visible = VisibleRows();
                if (!visible.empty() &&
                    std::find(visible.begin(), visible.end(), static_cast<std::size_t>(m_selRow)) == visible.end()) {
                    m_selRow = static_cast<int>(visible.front());
                }
            }
            Invalidate();
        }

        void MoveSelection(const int col, const int row) {
            if (m_rows.empty() || m_columns.empty()) return;
            const auto visible = VisibleRows();
            if (visible.empty()) return;
            int vi = 0;
            for (std::size_t i = 0; i < visible.size(); ++i) {
                if (visible[i] == static_cast<std::size_t>(m_selRow)) { vi = static_cast<int>(i); break; }
            }
            vi = std::clamp(vi + (row > m_selRow ? 1 : row < m_selRow ? -1 : 0), 0, static_cast<int>(visible.size()) - 1);
            m_selRow = static_cast<int>(visible[static_cast<std::size_t>(vi)]);
            m_selCol = std::clamp(col, 0, static_cast<int>(m_columns.size()) - 1);
            Invalidate();
        }

        void BeginEditSelected() {
            if (!editable || m_selRow < 0 || m_selCol < 0) return;
            if (!CellEditable(static_cast<std::size_t>(m_selCol))) return;
            BeginEdit(static_cast<std::size_t>(m_selRow), static_cast<std::size_t>(m_selCol));
        }

        void BeginEdit(const std::size_t row, const std::size_t col) {
            if (!CellEditable(col) || row >= m_rows.size()) return;
            m_editRow = static_cast<int>(row);
            m_editCol = static_cast<int>(col);
            m_editBuffer = m_rows[row][col];
            m_editing = true;
            Invalidate();
        }

        void CommitEdit() {
            if (!m_editing || m_editRow < 0 || m_editCol < 0) { m_editing = false; return; }
            const auto r = static_cast<std::size_t>(m_editRow);
            const auto c = static_cast<std::size_t>(m_editCol);
            if (r < m_rows.size() && c < m_columns.size()) {
                if (m_rows[r][c] != m_editBuffer) {
                    m_rows[r][c] = m_editBuffer;
                    if (onCellChanged) onCellChanged(r, c, m_rows[r][c]);
                }
            }
            m_editing = false;
            m_editRow = m_editCol = -1;
            m_filterDirty = true;
            InvalidateLayout();
            Invalidate();
        }

        void CancelEdit() {
            m_editing = false;
            m_editRow = m_editCol = -1;
            Invalidate();
        }

        [[nodiscard]] Style CellStyle(const std::size_t row, const std::size_t col, const bool editingCell) const {
            Style s = style;
            if (headerRow) {
                // header row 不在这里；该函数只处理数据行
            }
            if (editingCell) s = editStyle;
            else if (static_cast<int>(row) == m_selRow && static_cast<int>(col) == m_selCol && focusedCell()) {
                s = selectionStyle;
            }
            else if (striped && static_cast<int>(row) % 2 == 1) {
                s.bg = Theme::Current().surfaceAlt;
            }
            return s;
        }

        [[nodiscard]] bool focusedCell() const { return focused; }

        void BuildVisible() const {
            if (!m_filterDirty) return;
            m_filterDirty = false;
            m_visible.clear();
            m_visible.reserve(m_rows.size());
            if (!HasFilter() || m_filterColumn < 0) {
                for (std::size_t i = 0; i < m_rows.size(); ++i) m_visible.push_back(i);
                return;
            }
            const auto needle = LowerAscii(m_filterText);
            for (std::size_t i = 0; i < m_rows.size(); ++i) {
                const auto text = LowerAscii(PlainCell(i, static_cast<std::size_t>(m_filterColumn)));
                if (text.find(needle) != std::string::npos) m_visible.push_back(i);
            }
        }

        static std::string LowerAscii(std::string s) {
            for (auto& ch : s) {
                if (static_cast<unsigned char>(ch) < 0x80) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return s;
        }

        void DrawFrame(ConsoleBackBuffer& buf, const int visibleCount) {
            const auto chars = Box::Effective(Box::Single);
            const int l = Left();
            const int t = Top();
            const int w = TableWidth();
            const int dataTop = DataTop();
            const int bottom = dataTop + visibleCount;

            // 表格区域底色
            buf.Fill(Bounds{ l, t, w, bottom - t + 1 }, L' ', style);
            if (!showGrid) {
                // 无网格模式：只画表头分隔线
                if (headerRow) DrawHeader(buf, l, t, w);
                for (int i = 0; i < visibleCount; ++i) {
                    const auto row = VisibleRows()[static_cast<std::size_t>(i)];
                    DrawDataRow(buf, row, l, dataTop + i, w);
                }
                return;
            }

            // 顶边
            buf.SetCell(l, t, chars.topLeft, gridStyle);
            int x = l + 1;
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                const int cw = ColumnWidth(static_cast<int>(c));
                for (int k = 0; k < cw; ++k) buf.SetCell(x++, t, chars.horizontal, gridStyle);
                if (c + 1 < m_columns.size()) buf.SetCell(x++, t, chars.topTee, gridStyle);
            }
            buf.SetCell(l + w - 1, t, chars.topRight, gridStyle);

            if (headerRow) {
                DrawHeader(buf, l, t + 1, w);
                // 表头分隔线
                buf.SetCell(l, t + 2, chars.leftTee, gridStyle);
                int hx = l + 1;
                for (std::size_t c = 0; c < m_columns.size(); ++c) {
                    const int cw = ColumnWidth(static_cast<int>(c));
                    for (int k = 0; k < cw; ++k) buf.SetCell(hx++, t + 2, chars.horizontal, gridStyle);
                    if (c + 1 < m_columns.size()) buf.SetCell(hx++, t + 2, chars.cross, gridStyle);
                }
                buf.SetCell(l + w - 1, t + 2, chars.rightTee, gridStyle);
            }

            const auto visible = VisibleRows();
            for (int i = 0; i < visibleCount; ++i) {
                DrawDataRow(buf, visible[static_cast<std::size_t>(i)], l, dataTop + i, w);
            }

            // 底边
            buf.SetCell(l, bottom, chars.bottomLeft, gridStyle);
            x = l + 1;
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                const int cw = ColumnWidth(static_cast<int>(c));
                for (int k = 0; k < cw; ++k) buf.SetCell(x++, bottom, chars.horizontal, gridStyle);
                if (c + 1 < m_columns.size()) buf.SetCell(x++, bottom, chars.bottomTee, gridStyle);
            }
            buf.SetCell(l + w - 1, bottom, chars.bottomRight, gridStyle);
        }

        void DrawHeader(ConsoleBackBuffer& buf, const int l, const int y, const int w) {
            const auto chars = Box::Effective(Box::Single);
            buf.Fill(Bounds{ l + 1, y, w - 2, 1 }, L' ', headerStyle);
            buf.SetCell(l, y, chars.vertical, gridStyle);
            buf.SetCell(l + w - 1, y, chars.vertical, gridStyle);

            int x = l + 1;
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                const int cw = ColumnWidth(static_cast<int>(c));
                DrawCellText(buf, x, y, cw, m_columns[c].title, headerStyle, LayoutPos::Center);
                x += cw;
                if (c + 1 < m_columns.size()) {
                    buf.SetCell(x, y, chars.vertical, gridStyle);
                    ++x;
                }
            }
        }

        void DrawDataRow(ConsoleBackBuffer& buf, const std::size_t row, const int l, const int y, const int w) {
            const auto chars = Box::Effective(Box::Single);
            buf.Fill(Bounds{ l + 1, y, w - 2, 1 }, L' ', style);
            buf.SetCell(l, y, chars.vertical, gridStyle);
            buf.SetCell(l + w - 1, y, chars.vertical, gridStyle);

            int x = l + 1;
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                const int cw = ColumnWidth(static_cast<int>(c));
                const bool editing = m_editing &&
                    static_cast<int>(row) == m_editRow && static_cast<int>(c) == m_editCol;
                const Style cellStyle = CellStyle(row, c, editing);
                const std::string& raw = editing ? m_editBuffer : m_rows[row][c];
                std::string shown = raw;
                if (editing) shown += "|";   // 简易编辑光标
                DrawCellText(buf, x, y, cw, shown, cellStyle, m_columns[c].align);
                x += cw;
                if (c + 1 < m_columns.size()) {
                    buf.SetCell(x, y, chars.vertical, gridStyle);
                    ++x;
                }
            }
        }

        void DrawCellText(ConsoleBackBuffer& buf, const int x, const int y, const int width,
            const std::string& raw, const Style& st, const LayoutPos align) {
            if (width <= 0) return;
            const TStr rich = TStrFmt::Parse(raw, st);
            const int tw = rich.Width();
            int off = 0;
            if (align == LayoutPos::Center) off = std::max((width - tw) / 2, 0);
            else if (align == LayoutPos::Right) off = std::max(width - tw, 0);
            if (off > width - 1) off = width - 1;

            // 每个单元格单独裁剪，防止长文本/中文/换行串到下一列
            buf.PushClip(Bounds{ x, y, width, 1 });
            rich.WriteTo(buf, x + off, y);
            buf.PopClip();
        }

    private:
        std::vector<Column> m_columns{};
        std::vector<std::vector<std::string>> m_rows{};

        Style gridStyle{};
        Style headerStyle{};
        Style selectionStyle{};
        Style editStyle{};

        int m_selRow{ 0 };
        int m_selCol{ 0 };
        bool m_editing{ false };
        int m_editRow{ -1 };
        int m_editCol{ -1 };
        std::string m_editBuffer{};

        int m_filterColumn{ -1 };
        std::string m_filterText{};
        mutable bool m_filterDirty{ true };
        mutable std::vector<std::size_t> m_visible{};

        bool ImportDelimited(const std::string& path, const char delimiter, const bool firstRowHeader);
        void ApplyImported(const std::vector<std::vector<std::string>>& records, const bool firstRowHeader);
        void EnsureColumnCount(const std::size_t count);
        static std::string XmlEscape(const std::string& text);
        static std::string XmlUnescape(const std::string& text);
    };

    //------------------------------------------------------------------
    // 导入 / 导出
    //------------------------------------------------------------------
    inline bool Table::ImportCsv(const std::string& path, const bool firstRowHeader) {
        return ImportDelimited(path, ',', firstRowHeader);
    }

    inline bool Table::ImportDelimited(const std::string& path, const char delimiter, const bool firstRowHeader) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;

        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!data.empty() && static_cast<unsigned char>(data[0]) == 0xEF) {
            if (data.size() >= 3 &&
                static_cast<unsigned char>(data[1]) == 0xBB &&
                static_cast<unsigned char>(data[2]) == 0xBF) data.erase(0, 3);
        }

        std::vector<std::vector<std::string>> records;
        std::vector<std::string> row;
        std::string field;
        bool quoted = false;
        bool rowHasContent = false;

        for (std::size_t i = 0; i < data.size(); ++i) {
            const char ch = data[i];
            if (quoted) {
                if (ch == '"') {
                    if (i + 1 < data.size() && data[i + 1] == '"') {
                        field.push_back('"');
                        ++i;
                    }
                    else quoted = false;
                }
                else field.push_back(ch);
                continue;
            }
            if (ch == '"') { quoted = true; rowHasContent = true; continue; }
            if (ch == delimiter) {
                row.push_back(field);
                field.clear();
                rowHasContent = true;
                continue;
            }
            if (ch == '\n') {
                row.push_back(field);
                field.clear();
                records.push_back(row);
                row.clear();
                rowHasContent = false;
                continue;
            }
            if (ch == '\r') continue;
            field.push_back(ch);
            if (!std::isspace(static_cast<unsigned char>(ch))) rowHasContent = true;
        }
        if (!field.empty() || !row.empty() || rowHasContent) {
            row.push_back(field);
            records.push_back(row);
        }

        ApplyImported(records, firstRowHeader);
        return true;
    }

    inline void Table::EnsureColumnCount(const std::size_t count) {
        while (m_columns.size() < count) {
            const std::size_t idx = m_columns.size() + 1;
            AddColumn("C" + std::to_string(idx), 12);
        }
    }

    inline void Table::ApplyImported(const std::vector<std::vector<std::string>>& records,
        const bool firstRowHeader) {
        if (records.empty()) {
            m_rows.clear();
            InvalidateLayout();
            return;
        }

        std::size_t width = 0;
        for (const auto& r : records) width = std::max(width, r.size());

        if (firstRowHeader) {
            std::vector<std::string> header = records.front();
            EnsureColumnCount(width);
            for (std::size_t c = 0; c < header.size(); ++c) m_columns[c].title = header[c];
            if (header.size() < m_columns.size()) {
                for (std::size_t c = header.size(); c < m_columns.size(); ++c)
                    m_columns[c].title = "C" + std::to_string(c + 1);
            }
            m_rows.clear();
            for (std::size_t i = 1; i < records.size(); ++i) AddRow(records[i]);
        }
        else {
            EnsureColumnCount(width);
            m_rows.clear();
            for (const auto& r : records) AddRow(r);
        }
        AutoFitColumns(30);
        ClampSelection();
        InvalidateLayout();
        Invalidate();
    }

    inline void Table::AutoFitColumns(const int maxWidth) {
        if (m_columns.empty()) return;
        for (std::size_t c = 0; c < m_columns.size(); ++c) {
            int w = Utf8::DisplayWidth(PlainCell(0, c));
            for (std::size_t r = 0; r < m_rows.size(); ++r) {
                w = std::max(w, Utf8::DisplayWidth(PlainCell(r, c)));
            }
            m_columns[c].width = std::clamp(w + 2, 4, std::max(maxWidth, 4));
        }
        InvalidateLayout();
    }

    inline bool Table::ExportCsv(const std::string& path) const {
        std::ofstream file(path, std::ios::binary);
        if (!file) return false;

        // Excel 对无 BOM 的 UTF-8 CSV 可能误判编码；这里写 BOM，ImportDelimited 会自动剥离。
        file.write("\xEF\xBB\xBF", 3);

        auto quote = [](const std::string& field) {
            const bool need = field.find(',') != std::string::npos ||
                field.find('"') != std::string::npos ||
                field.find('\n') != std::string::npos ||
                field.find('\r') != std::string::npos;
            if (!need) return field;
            std::string out = "\"";
            for (const char c : field) {
                if (c == '"') out += "\"\"";
                else out.push_back(c);
            }
            out.push_back('"');
            return out;
        };

        if (headerRow) {
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                if (c > 0) file << ',';
                file << quote(m_columns[c].title);
            }
            file << "\r\n";
        }
        for (const auto& row : m_rows) {
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                if (c > 0) file << ',';
                file << quote(c < row.size() ? row[c] : std::string{});
            }
            file << "\r\n";
        }
        return true;
    }

    inline std::string Table::XmlEscape(const std::string& text) {
        std::string out;
        out.reserve(text.size() + 16);
        for (const char c : text) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c); break;
            }
        }
        return out;
    }

    inline std::string Table::XmlUnescape(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        std::size_t p = 0;
        while (p < text.size()) {
            if (text[p] != '&') { out.push_back(text[p++]); continue; }
            const auto semi = text.find(';', p);
            if (semi == std::string::npos) { out.push_back(text[p++]); continue; }
            const std::string ent = text.substr(p, semi - p + 1);
            if (ent == "&amp;") out.push_back('&');
            else if (ent == "&lt;") out.push_back('<');
            else if (ent == "&gt;") out.push_back('>');
            else if (ent == "&quot;") out.push_back('"');
            else if (ent == "&apos;") out.push_back('\'');
            else out += ent;
            p = semi + 1;
        }
        return out;
    }

    inline bool Table::ExportExcel(const std::string& path) const {
        const auto extPos = path.find_last_of('.');
        const std::string ext = (extPos == std::string::npos) ? std::string{} : LowerAscii(path.substr(extPos));
        if (ext == ".csv") return ExportCsv(path);
        if (ext != ".xml") return false;

        std::ofstream file(path, std::ios::binary);
        if (!file) return false;

        file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<?mso-application progid=\"Excel.Sheet\"?>\n"
            << "<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\" "
            << "xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\n"
            << "<Worksheet ss:Name=\"Sheet1\"><Table>\n";

        auto writeRow = [&](const std::vector<std::string>& row) {
            file << "<Row>";
            for (std::size_t c = 0; c < m_columns.size(); ++c) {
                file << "<Cell><Data ss:Type=\"String\">"
                    << XmlEscape(c < row.size() ? row[c] : std::string{})
                    << "</Data></Cell>";
            }
            file << "</Row>\n";
        };

        if (headerRow) {
            std::vector<std::string> header;
            for (const auto& c : m_columns) header.push_back(c.title);
            writeRow(header);
        }
        for (const auto& row : m_rows) writeRow(row);

        file << "</Table></Worksheet></Workbook>\n";
        return true;
    }

    inline bool Table::ImportExcel(const std::string& path) {
        const auto extPos = path.find_last_of('.');
        const std::string ext = (extPos == std::string::npos) ? std::string{} : LowerAscii(path.substr(extPos));
        if (ext == ".csv") return ImportCsv(path, true);
        if (ext == ".tsv") return ImportDelimited(path, '\t', true);
        if (ext != ".xml") return false;

        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        std::vector<std::vector<std::string>> records;
        std::size_t rowPos = 0;
        while (true) {
            rowPos = data.find("<Row", rowPos);
            if (rowPos == std::string::npos) break;
            const auto rowEnd = data.find("</Row>", rowPos);
            if (rowEnd == std::string::npos) break;

            std::vector<std::string> row;
            std::size_t cellPos = rowPos;
            while (true) {
                cellPos = data.find("<Cell", cellPos);
                if (cellPos == std::string::npos || cellPos >= rowEnd) break;
                const auto cellEnd = data.find("</Cell>", cellPos);
                if (cellEnd == std::string::npos || cellEnd > rowEnd) break;

                const auto dataPos = data.find("<Data", cellPos);
                if (dataPos != std::string::npos && dataPos < cellEnd) {
                    const auto textStart = data.find('>', dataPos);
                    const auto textEnd = data.find("</Data>", textStart == std::string::npos ? dataPos : textStart);
                    if (textStart != std::string::npos && textEnd != std::string::npos && textEnd <= cellEnd) {
                        row.push_back(XmlUnescape(data.substr(textStart + 1, textEnd - textStart - 1)));
                    }
                    else row.emplace_back();
                }
                else row.emplace_back();
                cellPos = cellEnd + 7;
            }
            records.push_back(row);
            rowPos = rowEnd + 6;
        }

        ApplyImported(records, true);
        return !records.empty();
    }

} // namespace Terminal
