#include "stdafx.h"
#include "ClassicalTaskbarDlg.h"

namespace
{
    // 任务栏里的 MSTaskSwWClass 属于 explorer。同步 MoveWindow/SetWindowPos 会
    // SendMessage 到 explorer；explorer 同时又在等本进程的任务栏子窗口，两边互相等就会把任务栏卡死。
    // SWP_ASYNCWINDOWPOS 改成投递，不再在这里死等。尺寸已经对上就不再发，避免和 StartAllBack 每 100ms 对打。
    void MoveExplorerWindowAsync(HWND hwnd, int x, int y, int cx, int cy)
    {
        if (hwnd == nullptr || !::IsWindow(hwnd))
            return;

        RECT rc{};
        if (::GetWindowRect(hwnd, &rc)
            && (rc.right - rc.left) == cx
            && (rc.bottom - rc.top) == cy)
        {
            return;
        }

        static HWND last_hwnd{};
        static int last_x{};
        static int last_y{};
        static int last_cx{};
        static int last_cy{};
        static DWORD last_tick{};
        const DWORD now = ::GetTickCount();
        if (hwnd == last_hwnd && x == last_x && y == last_y && cx == last_cx && cy == last_cy
            && now - last_tick < 500)
        {
            return;
        }
        last_hwnd = hwnd;
        last_x = x;
        last_y = y;
        last_cx = cx;
        last_cy = cy;
        last_tick = now;

        ::SetWindowPos(hwnd, nullptr, x, y, cx, cy,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    }
}

void CClassicalTaskbarDlg::AdjustTaskbarWndPos(bool force_adjust)
{
    ::GetWindowRect(m_hMin, m_rcMin); //获得最小化窗口的区域
    ::GetWindowRect(m_hBar, m_rcBar); //获得二级容器的区域

    if (m_taskbar_on_top_or_bottom)     //当任务栏在桌面顶部或底部时
    {
        //设置窗口大小
        m_rect.right = m_rect.left + m_window_width;
        m_rect.bottom = m_rect.top + m_window_height;

        if (force_adjust || m_rcMin.Width() != m_last_width)   //如果宽度改变了，重新设置任务栏窗口的位置
        {
            m_rcMinOri = m_rcMin;
            m_left_space = m_rcMin.left - m_rcBar.left;
            //保存上次的宽度
            m_last_width = m_rcMin.Width() - m_rect.Width();
            //任务窗口显示在右侧时
            if (!theApp.m_taskbar_data.tbar_wnd_on_left)
            {
                MoveExplorerWindowAsync(m_hMin, m_left_space, 0, m_rcMin.Width() - m_rect.Width(), m_rcMin.Height());    //设置最小化窗口的位置
                m_rect.MoveToX(m_left_space + m_rcMin.Width() - m_rect.Width() + 2);
            }
            //任务栏窗口显示在左侧时
            else
            {
                MoveExplorerWindowAsync(m_hMin, m_left_space + m_rect.Width(), 0, m_rcMin.Width() - m_rect.Width(), m_rcMin.Height());
                m_rect.MoveToX(m_left_space);
            }

            //设置任务栏窗口的垂直位置
            m_rect.MoveToY((m_rcBar.Height() - m_rect.Height()) / 2);
            if (theApp.m_taskbar_data.horizontal_arrange && theApp.m_win_version.IsWindows7())
                m_rect.MoveToY(m_rect.top + DPI(1));
            MoveWindow(m_rect);
        }
    }
    else        //当任务栏在屏幕在左侧或右侧时
    {
        //设置窗口大小
        if (force_adjust || m_rcMin.Height() != m_last_height)     //如果高度改变了，重新设置任务栏窗口的位置
        {
            m_rcMinOri = m_rcMin;
            m_top_space = m_rcMin.top - m_rcBar.top;
            //保存上次的宽度
            m_last_height = m_rcMin.Height() - m_rect.Height();

            if (!theApp.m_taskbar_data.tbar_wnd_on_left)
            {
                MoveExplorerWindowAsync(m_hMin, 0, m_top_space, m_rcMin.Width(), m_rcMin.Height() - m_rect.Height());    //设置最小化窗口的位置
                m_rect.MoveToY(m_top_space + m_rcMin.Height() - m_rect.Height() + 2);
            }
            else
            {
                MoveExplorerWindowAsync(m_hMin, 0, m_top_space + m_rect.Height(), m_rcMin.Width(), m_rcMin.Height() - m_rect.Height());  //设置最小化窗口的位置
                m_rect.MoveToY(m_top_space);
            }
            m_rect.MoveToX((m_rcMin.Width() - m_window_width) / 2);
            int left_space = DPI(2);
            if (m_rect.left < left_space)
                m_rect.MoveToX(left_space);
            MoveWindow(m_rect);
        }
    }
}

void CClassicalTaskbarDlg::InitTaskbarWnd()
{
    m_hBar = ::FindWindowEx(m_hTaskbar, 0, L"ReBarWindow32", NULL); //寻找二级容器的句柄
    if (m_hBar == NULL)
        m_hBar = ::FindWindowEx(m_hTaskbar, nullptr, L"WorkerW", NULL);
    m_hMin = ::FindWindowEx(m_hBar, 0, L"MSTaskSwWClass", NULL);    //寻找最小化窗口的句柄
    if (m_hMin == NULL)
        m_hMin = ::FindWindowEx(m_hBar, 0, L"MSTaskListWClass", NULL);    //寻找最小化窗口的句柄

    ::GetWindowRect(m_hMin, m_rcMin);   //获得最小化窗口的区域
    ::GetWindowRect(m_hBar, m_rcBar);   //获得二级容器的区域

    m_left_space = m_rcMin.left - m_rcBar.left;
    m_top_space = m_rcMin.top - m_rcBar.top;
}

void CClassicalTaskbarDlg::ResetTaskbarPos()
{
    //程序关闭的时候，把最小化窗口的width恢复回去
    if (!m_rcMinOri.IsRectEmpty())
    {
        if (m_taskbar_on_top_or_bottom)
            MoveExplorerWindowAsync(m_hMin, m_left_space, 0, m_rcMinOri.Width(), m_rcMinOri.Height());
        else

            MoveExplorerWindowAsync(m_hMin, 0, m_top_space, m_rcMinOri.Width(), m_rcMinOri.Height());
    }
}

HWND CClassicalTaskbarDlg::GetParentHwnd()
{
    return m_hBar;
}

void CClassicalTaskbarDlg::CheckTaskbarOnTopOrBottom()
{
    CRect rect;
    CRect rcMin;
    CRect rcBar;
    if (m_hTaskbar != 0)
    {
        //::GetWindowRect(m_hMin, rcMin); //获得最小化窗口的区域
        //::GetWindowRect(m_hBar, rcBar); //获得二级容器的区域
        //if (m_left_space == 0)
        //    m_left_space = rcMin.left - rcBar.left;
        //if (m_top_space == 0)
        //    m_top_space = rcMin.top - rcBar.top;

        ::GetWindowRect(m_hTaskbar, rect);          //获取任务栏的矩形区域
        m_taskbar_on_top_or_bottom = (rect.Width() >= rect.Height());     //如果任务栏的宽度大于高度，则任务在屏幕的顶部或底部
    }
    else
    {
        m_taskbar_on_top_or_bottom = true;
    }
}

