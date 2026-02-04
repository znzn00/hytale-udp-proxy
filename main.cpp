#include <wx/wxprec.h>

#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/simplebook.h>
#include <wx/statline.h>
#include "sqlite3.h"
#include "proxy_common.h"
#include "ipv4_proxy.h"
#include "ipv6_proxy.h"
#include <wx/artprov.h>
#include <wx/clipbrd.h>

#define SERVER_NAME_WXCOLOR wxColor(10, 100, 200)
#define SERVER_PORT_WXCOLOR wxColor(140, 140, 140)
#define SERVER_MOVE_UP -1
#define SERVER_MOVE_DOWN 1

class MyApp : public wxApp
{
    sqlite3 *db;
    WSAData wsaData;

public:
    virtual bool OnInit();
    virtual int OnExit() override;
};

typedef struct
{
    int id;
    std::string name;
    eAddressType address_type;
    std::string address;
    int port;
} ServerRecord;

wxDEFINE_EVENT(wxEVT_CONNECT_SERVER_RECORD, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_DELETE_SERVER_RECORD, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_MOVE_SERVER_RECORD, wxThreadEvent);
// Disable on connection
wxDEFINE_EVENT(wxEVT_SET_ENABLE_INPUT, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_PROXY_THREAD_UPDATE, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_PROXY_THREAD_RESOLVED_ADDRESS, wxThreadEvent);
wxDEFINE_EVENT(wxEVT_PROXY_THREAD_STOPPED, wxThreadEvent);

class ProxyThread : public wxThread
{
public:
    ProxyThread(wxWindow *parent, int proxy_port, ServerRecord server_record);
    ~ProxyThread();

protected:
    virtual ExitCode Entry();

private:
    int proxy_port;
    ServerRecord server_record;
    wxWindow *parent;
};

class MainFrame : public wxFrame
{
public:
    MainFrame(sqlite3 *db);

protected:
    ProxyThread *proxy_thread;
    wxCriticalSection m_pThreadCS;
    friend class ProxyThread;

private:
    sqlite3 *db;
    void OnPortUpdate(wxFocusEvent &event);
    wxTextCtrl *ptr_port_input;
    wxTextCtrl *ptr_ip_input;
    wxButton *ptr_connect_button;
    wxButton *ptr_save_button;
    wxButton *ptr_stop_proxy_button;
    wxSimplebook *status_book;
    wxScrolledWindow *server_list;
    wxTextCtrl *proxy_server_address_ptr;
    wxTextCtrl *proxy_resolved_server_address_ptr;
    wxBoxSizer *proxy_server_address_sizer_ptr;
    wxStaticText *profile_name_ptr;
    wxTextCtrl *profile_address_ptr;
    wxButton *copy_proxy_address_button_ptr;
    int port;

    void OnDirectConnect(wxCommandEvent &event);
    void OnSave(wxCommandEvent &event);
    void RenderServerRecord(ServerRecord record);

    int ValidateServerAddress(eAddressType address_type, std::string address, int port);
    std::vector<ServerRecord> LoadServerRecordFromSql();

    // Handle
    void MoveServerRecord(wxThreadEvent &event);
    void ConnectFromServerRecord(wxThreadEvent &event);
    void DeleteServerRecord(wxThreadEvent &event);
    void OnProxyThreadStopped(wxThreadEvent &event);
    void OnProxyThreadUpdate(wxThreadEvent &event);
    void OnProxyThreadResolvedAddress(wxThreadEvent &event);
    void StopProxy();
    void OnStopProxy(wxCommandEvent &event);
    void OnCopyProxyAddress(wxCommandEvent &event);
    void OnClose(wxCloseEvent &event);
};

class ServerWidget : public wxPanel
{
private:
    ServerRecord record;
    void OnConnect(wxCommandEvent &event);
    void OnDelete(wxCommandEvent &event);
    void OnMoveUp(wxCommandEvent &event);
    void OnMoveDown(wxCommandEvent &event);
    void CopyAddress(wxCommandEvent &event);
    wxButton *connect_button;

public:
    ServerWidget(wxWindow *parent, ServerRecord record);
    int GetRecordId();
    ServerRecord GetRecord();
    std::string GetFullAddress();
    std::string GetMinimalAddress();
    bool SetConnectButtonEnable(bool state);
    void ExchangeRecordId(ServerWidget *other);
};

class DeleteServerRecordDialog : public wxDialog
{
public:
    DeleteServerRecordDialog(ServerRecord record,
                             const wxString &caption = wxASCII_STR(wxMessageBoxCaptionStr),
                             long style = wxOK | wxCENTRE,
                             wxWindow *parent = NULL,
                             int x = wxDefaultCoord, int y = wxDefaultCoord);
};

wxIMPLEMENT_APP(MyApp);

ProxyThread::ProxyThread(wxWindow *parent, int proxy_port, ServerRecord server_record) : wxThread(wxTHREAD_DETACHED)
{
    this->parent = parent;
    this->proxy_port = proxy_port;
    this->server_record = server_record;
}

wxThread::ExitCode ProxyThread::Entry()
{
    eAddressType mode = eAddressType::Invalid;
    wxThreadEvent *threadEvent;
    struct in_addr serverIp4;
    struct in6_addr serverIp6;
    struct addrinfo *result = nullptr;
    struct addrinfo *p = nullptr;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    if (this->server_record.address_type == eAddressType::Domain)
    {
        std::string address_str = this->server_record.address;
        int status = getaddrinfo(address_str.c_str(), NULL, &hints, &result);
        if (status != 0)
        {
            fprintf(stderr, "For domain (%s) error in getaddrinfo: %s\n", this->server_record.address.c_str(), gai_strerror(status));
            threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_STOPPED);
            threadEvent->SetInt(1);
            threadEvent->SetString(this->server_record.address);
            wxQueueEvent(this->parent, threadEvent);
            return (wxThread::ExitCode)0;
        }

        for (p = result; p != NULL; p = p->ai_next)
        {
            // void *addr;

            if (p->ai_family == AF_INET)
            { // IPv4
                struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
                if (test_ipv4_quic(ipv4->sin_addr, this->server_record.port) == 0)
                {
                    serverIp4 = (ipv4->sin_addr);
                    mode = eAddressType::IPv4;
                    break;
                }
            }
            else
            { // IPv6
                struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>(p->ai_addr);
                if (test_ipv6_quic(ipv6->sin6_addr, this->server_record.port) == 0)
                {
                    serverIp6 = (ipv6->sin6_addr);
                    mode = eAddressType::IPv6;
                    break;
                }
            }
        }
        freeaddrinfo(result);
        if (mode == eAddressType::Invalid)
        {
            threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_STOPPED);
            threadEvent->SetInt(2);
            threadEvent->SetString(this->server_record.address);
            wxQueueEvent(this->parent, threadEvent);
            return (wxThread::ExitCode)0;
        }

        if (mode == eAddressType::IPv6)
        {
            threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_RESOLVED_ADDRESS);
            char ipv6[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &serverIp6, ipv6, sizeof(ipv6));
            threadEvent->SetString("[" + std::string(ipv6) + "]:" + std::to_string(this->server_record.port));
            wxQueueEvent(this->parent, threadEvent);
        }
        else
        {
            threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_RESOLVED_ADDRESS);
            char ipv4[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &serverIp4, ipv4, sizeof(ipv4));
            threadEvent->SetString(std::string(ipv4) + ":" + std::to_string(this->server_record.port));
            wxQueueEvent(this->parent, threadEvent);
        }
    }
    else if (this->server_record.address_type == eAddressType::IPv4)
    {
        if (inet_pton(AF_INET, this->server_record.address.c_str(), &serverIp4) == 1)
        {
            mode = eAddressType::IPv4;
        }
    }
    else if (this->server_record.address_type == eAddressType::IPv6)
    {
        if (inet_pton(AF_INET6, this->server_record.address.c_str(), &serverIp6) == 1)
        {
            mode = eAddressType::IPv6;
        }
    }

    if (mode == eAddressType::Invalid)
    {
        threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_STOPPED);
        threadEvent->SetInt(5);
        wxQueueEvent(this->parent, threadEvent);
        return (wxThread::ExitCode)0;
    }

    int proxySocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (proxySocket < 0)
    {
        perror("Proxy socket creation failed");
        threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_STOPPED);
        threadEvent->SetInt(3);
        wxQueueEvent(this->parent, threadEvent);
        return (wxThread::ExitCode)0;
    }
    sockaddr_in proxyAddress;
    proxyAddress.sin_family = AF_INET;
    proxyAddress.sin_port = htons(this->proxy_port);
    proxyAddress.sin_addr.s_addr = INADDR_ANY;
    if (bind(proxySocket, (struct sockaddr *)&proxyAddress, sizeof(proxyAddress)) == -1)
    {
        perror("Proxy bind failed");
        close_socket(proxySocket);
        threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_STOPPED);
        threadEvent->SetInt(4);
        threadEvent->SetString(std::to_string(this->proxy_port));
        wxQueueEvent(this->parent, threadEvent);
        return (wxThread::ExitCode)0;
    }

#ifdef _WIN32
    int timeoutMs = 10000;
    setsockopt(proxySocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(proxySocket, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&tv, sizeof(tv));
#endif
    char ipAddress[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(proxyAddress.sin_addr), ipAddress, INET_ADDRSTRLEN);
    std::cout << "Proxy [" << proxySocket << "] bound to \"" << ipAddress << ":" << ntohs(proxyAddress.sin_port) << "\"." << std::endl;

    int proxy_state = PROXY_IDDLE;
    int target_port = this->server_record.port;
    int temp;
    if (mode == eAddressType::IPv4)
    {
        IPv4Proxy proxy_v4(proxySocket);
        std::thread t([this, &proxy_v4, serverIp4, target_port]()
                      { proxy_v4.connect(serverIp4, target_port); });
        t.detach();

        while (proxy_v4.get_state() != PROXY_READY)
        {
            this->Sleep(5);
        }

        while (true)
        {
            temp = proxy_v4.get_state();
            if (temp != proxy_state)
            {
                proxy_state = temp;
                if (proxy_state == PROXY_READY)
                {
                    threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_UPDATE);
                    threadEvent->SetInt(1);
                    threadEvent->SetString("localhost:" + std::to_string(this->proxy_port));
                    wxQueueEvent(this->parent, threadEvent);
                }
                else if (proxy_state == PROXY_ESTABLISHED)
                {
                    threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_UPDATE);
                    threadEvent->SetInt(2);
                    wxQueueEvent(this->parent, threadEvent);
                }
            }
            this->Sleep(5); // Sleep for 5ms

            if (this->TestDestroy())
            {
                proxy_v4.disconnect();
                while (proxy_v4.is_running())
                {
                    this->Sleep(5);
                }
                break;
            }
            // wxQueueEvent(m_pHandler, new wxThreadEvent(wxEVT_COMMAND_MYTHREAD_UPDATE));
        }
    }
    else if (mode == eAddressType::IPv6)
    {
        IPv6Proxy proxy_v6(proxySocket);
        std::thread t([this, &proxy_v6, serverIp6, target_port]()
                      { proxy_v6.connect(serverIp6, target_port); });
        t.detach();

        while (true)
        {
            temp = proxy_v6.get_state();
            if (temp != proxy_state)
            {
                proxy_state = temp;
                if (proxy_state == PROXY_READY)
                {
                    threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_UPDATE);
                    threadEvent->SetInt(1);
                    threadEvent->SetString("localhost:" + std::to_string(this->proxy_port));
                    wxQueueEvent(this->parent, threadEvent);
                }
                else if (proxy_state == PROXY_ESTABLISHED)
                {
                    threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_UPDATE);
                    threadEvent->SetInt(2);
                    wxQueueEvent(this->parent, threadEvent);
                }
            }
            this->Sleep(5); // Sleep for 5ms

            if (this->TestDestroy())
            {
                proxy_v6.disconnect();
                while (proxy_v6.is_running())
                {
                    this->Sleep(5);
                }
                break;
            }
            // wxQueueEvent(m_pHandler, new wxThreadEvent(wxEVT_COMMAND_MYTHREAD_UPDATE));
        }
    }

    // return proxySocket;

    close_socket(proxySocket);
    threadEvent = new wxThreadEvent(wxEVT_PROXY_THREAD_STOPPED);
    threadEvent->SetInt(0);
    wxQueueEvent(this->parent, threadEvent);
    return (wxThread::ExitCode)0;
}

ProxyThread::~ProxyThread()
{
    MainFrame *mainFrame = wxStaticCast(this->parent, MainFrame);
    if (mainFrame)
    {
        wxCriticalSectionLocker enter(mainFrame->m_pThreadCS);
        mainFrame->proxy_thread = nullptr;
    }

    // the thread is being destroyed; make sure not to leave dangling pointers around
}

std::string ServerWidget::GetFullAddress()
{
    if (this->record.address_type == eAddressType::IPv6)
    {
        return "[" + this->record.address + "]:" + std::to_string(this->record.port);
    }
    return this->record.address + ":" + std::to_string(this->record.port);
}

std::string ServerWidget::GetMinimalAddress()
{
    std::string response;
    if (this->record.address_type == eAddressType::IPv6)
    {
        response = "[" + this->record.address + "]";
    }
    else
    {
        response = this->record.address;
    }
    if (record.port != PROXY_DEFAULT_PORT)
    {
        return response += ":" + std::to_string(this->record.port);
    }
    return response;
}

bool ServerWidget::SetConnectButtonEnable(bool state)
{
    if (state)
    {
        return this->connect_button->Enable();
    }
    return this->connect_button->Disable();
}

void ServerWidget::ExchangeRecordId(ServerWidget *other)
{
    std::swap(this->record.id, other->record.id);
}

int ServerWidget::GetRecordId()
{
    return this->record.id;
}

ServerRecord ServerWidget::GetRecord()
{
    return this->record;
}

DeleteServerRecordDialog::DeleteServerRecordDialog(ServerRecord record,
                                                   const wxString &title,
                                                   long style,
                                                   wxWindow *parent,
                                                   int x, int y) : wxDialog(parent, wxID_ANY, title, wxPoint{x, y}, wxDefaultSize)
{

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *padding = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *content = new wxBoxSizer(wxVERTICAL);
    content->Add(new wxStaticText(this, wxID_ANY, "Do you want to delete this server?"));

    wxBoxSizer *name_sizer = new wxBoxSizer(wxHORIZONTAL);
    name_sizer->Add(new wxStaticText(this, wxID_ANY, "Name: "));
    wxStaticText *wx_record_name = new wxStaticText(this, wxID_ANY, record.name);
    wx_record_name->SetFont(wxFont(wx_record_name->GetFont().GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    wx_record_name->SetForegroundColour(SERVER_NAME_WXCOLOR);
    name_sizer->Add(wx_record_name);
    content->Add(name_sizer);

    wxBoxSizer *address_sizer = new wxBoxSizer(wxHORIZONTAL);
    address_sizer->Add(new wxStaticText(this, wxID_ANY, "Address: "));
    wxStaticText *wx_address;
    if (record.address_type == eAddressType::IPv6)
    {
        wx_address = new wxStaticText(this, wxID_ANY, "[" + record.address + "]");
    }
    else
    {
        wx_address = new wxStaticText(this, wxID_ANY, record.address);
    }

    address_sizer->Add(wx_address);
    wxStaticText *wx_port = new wxStaticText(this, wxID_ANY, ":" + std::to_string(record.port));

    wx_port->SetFont(wxFont(wx_port->GetFont().GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    wx_port->SetForegroundColour(SERVER_PORT_WXCOLOR);
    address_sizer->Add(wx_port);
    content->Add(address_sizer);

    // Fetch the native 'Question' icon
    wxBitmap questionBitmap = wxArtProvider::GetBitmap(wxART_QUESTION, wxART_MESSAGE_BOX);

    // Display it in your dialog using a StaticBitmap
    wxStaticBitmap *icon = new wxStaticBitmap(this, wxID_ANY, questionBitmap);

    padding->Add(icon, 0, wxRIGHT, 5);
    padding->Add(content, 0);

    sizer->Add(padding, 0, wxTOP | wxRIGHT | wxLEFT, 10);

    wxSizer *buttonSizer = CreateButtonSizer(style);
    if (buttonSizer)
    {
        sizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 10);
    }
    SetSizerAndFit(sizer);
    CenterOnParent();

    this->Bind(wxEVT_BUTTON, [this](wxCommandEvent &event)
               { this->EndModal(event.GetId()); });

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &event)
               { this->EndModal(wxID_CLOSE); });
}

std::vector<ServerRecord> MainFrame::LoadServerRecordFromSql()
{
    std::vector<ServerRecord> records;
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, name, address_type, address, port FROM server;", -1, &stmt, NULL) != SQLITE_OK)
    {
        std::cerr << "Couldn't load servers from sqlite3." << std::endl;
        return records;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const char *rawName = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        std::string name = rawName ? rawName : "";
        eAddressType addr_type = static_cast<eAddressType>(sqlite3_column_int(stmt, 2));
        const char *rawAddr = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        std::string address = rawAddr ? rawAddr : "";
        int port = sqlite3_column_int(stmt, 4);
        records.push_back({id, name, addr_type, address, port});
    }

    sqlite3_finalize(stmt);
    return records;
}

int MainFrame::ValidateServerAddress(eAddressType address_type, std::string address, int port)
{
    if (address_type == eAddressType::Invalid)
    {
        wxMessageBox(
            L"It should be a valid domain, IPv4 or IPv6 optionally with port. Examples:\n"
            L"• Domain: example.com:9520\n"
            L"• IPv4: 192.168.1.1:9520\n"
            L"• IPv6: [2001:db8::1]:9520",
            "Invalid Server Address",
            wxOK | wxICON_ERROR);
        return 1;
    }

    if (port == -1)
    {
        wxMessageBox(
            "Port should be between [1-65535]",
            "Invalid Server Port",
            wxOK | wxICON_ERROR);
        return 1;
    }
    return 0;
}

void MainFrame::ConnectFromServerRecord(wxThreadEvent &event)
{
    if (this->port == -1)
    {
        wxMessageBox(
            "Port should be in range [1-65535]",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        return;
    }

    if (this->proxy_thread != nullptr)
    {
        return;
    }

    ServerRecord record = event.GetPayload<ServerRecord>();

    this->status_book->SetSelection(1);
    this->proxy_thread = new ProxyThread(this, this->port, record);

    if (this->proxy_thread->Run() != wxTHREAD_NO_ERROR)
    {
        wxLogError("Can't create the thread!");
        delete this->proxy_thread;
        this->proxy_thread = nullptr;
        this->status_book->SetSelection(0);
        return;
    }

    this->ptr_port_input->Disable();
    this->ptr_ip_input->Disable();
    this->ptr_connect_button->Disable();
    this->ptr_save_button->Disable();
    this->ptr_stop_proxy_button->Enable();
    if (record.id != 0)
    {
        this->profile_name_ptr->SetLabel(record.name);
        this->profile_name_ptr->SetForegroundColour(SERVER_NAME_WXCOLOR);
    }
    else
    {
        this->profile_name_ptr->SetLabel("Direct Connection");
    }

    if (record.address_type == eAddressType::IPv6)
    {
        this->proxy_server_address_ptr->SetValue("[" + record.address + "]:" + std::to_string(record.port));
    }
    else
    {
        this->proxy_server_address_ptr->SetValue(record.address + ":" + std::to_string(record.port));
    }

    wxSizer *server_sizer = this->server_list->GetSizer();
    wxSizerItemList &children = server_sizer->GetChildren();

    for (auto child : children)
    {
        ServerWidget *widget = wxDynamicCast(child->GetWindow(), ServerWidget);

        if (widget)
        {
            widget->SetConnectButtonEnable(false);
        }
    }
}

void MainFrame::DeleteServerRecord(wxThreadEvent &event)
{
    wxObject *origin = event.GetEventObject();
    ServerWidget *panel = wxDynamicCast(origin, ServerWidget);
    if (!panel)
    {
        return;
    }
    int record_id = panel->GetRecordId();

    sqlite3_stmt *stmt;
    char *error_msg;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &error_msg);
    if (sqlite3_prepare_v2(db, "DELETE FROM server WHERE id = ?;", -1, &stmt, NULL) != SQLITE_OK)
    {

        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, &error_msg);
        sqlite3_free(error_msg);
        return;
    }

    sqlite3_bind_int(stmt, 1, record_id);

    if (sqlite3_step(stmt) != SQLITE_DONE)
    {

        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, &error_msg);
        sqlite3_finalize(stmt);
        sqlite3_free(error_msg);
        return;
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, &error_msg);
    sqlite3_free(error_msg);
    sqlite3_finalize(stmt);
    if (panel->Destroy())
    {
        this->server_list->GetSizer()->Layout();
        this->server_list->FitInside();
    }
}
void MainFrame::MoveServerRecord(wxThreadEvent &event)
{
    wxObject *origin = event.GetEventObject();
    ServerWidget *panel = wxDynamicCast(origin, ServerWidget);

    if (!panel)
    {
        return;
    }
    int target = -1;
    wxBoxSizer *server_sizer = (wxBoxSizer *)this->server_list->GetSizer();
    int limit = (int)server_sizer->GetItemCount();

    for (int i = 0; i < limit; i++)
    {
        if (server_sizer->GetItem(i)->GetWindow() == panel)
        {
            target = i;
            break;
        }
    }

    if (target == -1)
    {
        return;
    }

    int move = event.GetInt();

    ServerWidget *exchange = NULL;
    int other;

    if (move == SERVER_MOVE_UP)
    {
        if (target == 0) // it's already on top
        {
            return;
        }
        other = target - 1;
        exchange = wxDynamicCast(server_sizer->GetItem(other)->GetWindow(), ServerWidget);
    }
    else if (move == SERVER_MOVE_DOWN)
    {
        int other = target + 1;
        if (other == limit) // it's already on bottom
        {
            return;
        }
        exchange = wxDynamicCast(server_sizer->GetItem(other)->GetWindow(), ServerWidget);
    }

    if (!exchange)
    {
        return;
    }

    ServerRecord panel_record = panel->GetRecord();
    ServerRecord exchange_record = exchange->GetRecord();
    std::swap(panel_record.id, exchange_record.id);

    char *error_msg = nullptr;

    // 1. Start the transaction
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &error_msg);

    const char *sql = "UPDATE server SET name = ?, address_type=?, address=?, port=? WHERE id = ?;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {

        sqlite3_bind_text(stmt, 1, panel_record.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(panel_record.address_type));
        sqlite3_bind_text(stmt, 3, panel_record.address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, panel_record.port);
        sqlite3_bind_int(stmt, 5, panel_record.id);
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return;
        }
        sqlite3_reset(stmt); // Reset is required to reuse the prepared statement

        sqlite3_bind_text(stmt, 1, exchange_record.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(exchange_record.address_type));
        sqlite3_bind_text(stmt, 3, exchange_record.address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, exchange_record.port);
        sqlite3_bind_int(stmt, 5, exchange_record.id);
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return;
        };

        sqlite3_finalize(stmt);

        // 4. Commit the changes to disk
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &error_msg);
    }
    else
    {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }

    panel->ExchangeRecordId(exchange);

    ServerWidget *to_rerender = move == SERVER_MOVE_UP ? panel : exchange;

    server_sizer->Detach(to_rerender);
    server_sizer->Insert(std::min(other, target), to_rerender, 0, wxALL | wxEXPAND);
    this->server_list->GetSizer()->Layout();
    this->server_list->FitInside();
}

void ServerWidget::OnConnect(wxCommandEvent &event)
{
    ServerRecord record = this->record;
    wxThreadEvent *request = new wxThreadEvent(wxEVT_CONNECT_SERVER_RECORD);
    request->SetEventObject(this);
    request->SetPayload(record);
    request->ResumePropagation(wxEVENT_PROPAGATE_MAX);
    wxQueueEvent(this, request);
}
void ServerWidget::OnDelete(wxCommandEvent &event)
{

    DeleteServerRecordDialog dialog(this->record,
                                    "Confirm Deletion",
                                    wxYES_NO | wxNO_DEFAULT);

    if (dialog.ShowModal() == wxID_YES)
    {
        wxThreadEvent *request = new wxThreadEvent(wxEVT_DELETE_SERVER_RECORD);
        request->SetEventObject(this);
        request->ResumePropagation(wxEVENT_PROPAGATE_MAX);
        wxQueueEvent(this, request);
    }
}
void ServerWidget::OnMoveUp(wxCommandEvent &event)
{
    wxThreadEvent *request = new wxThreadEvent(wxEVT_MOVE_SERVER_RECORD);
    request->SetEventObject(this);
    request->SetInt(SERVER_MOVE_UP);
    request->ResumePropagation(wxEVENT_PROPAGATE_MAX);
    wxQueueEvent(this, request);
}
void ServerWidget::OnMoveDown(wxCommandEvent &event)
{
    wxThreadEvent *request = new wxThreadEvent(wxEVT_MOVE_SERVER_RECORD);
    request->SetEventObject(this);
    request->SetInt(SERVER_MOVE_DOWN);
    request->ResumePropagation(wxEVENT_PROPAGATE_MAX);
    wxQueueEvent(this, request);
}

void ServerWidget::CopyAddress(wxCommandEvent &event)
{
    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(this->GetMinimalAddress()));
        wxTheClipboard->Close();
    }
}

ServerWidget::ServerWidget(wxWindow *parent, ServerRecord record) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE)
{
    this->record = record;
    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *padding = new wxBoxSizer(wxVERTICAL);
    wxStaticText *title = new wxStaticText(this, wxID_ANY, record.name, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    title->SetFont(wxFont(16, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(SERVER_NAME_WXCOLOR);
    sizer->Add(title, 0, wxEXPAND);
    wxBoxSizer *address = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText *address_text;
    switch (record.address_type)
    {
    case eAddressType::IPv4:
    case eAddressType::Domain:
        address_text = new wxStaticText(this, wxID_ANY, record.address);
        address_text->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_SEMIBOLD));
        break;
    case eAddressType::IPv6:
        address_text = new wxStaticText(this, wxID_ANY, "[" + record.address + "]");
        address_text->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_SEMIBOLD));
        break;
    default:
        address_text = new wxStaticText(this, wxID_ANY, "N/A");
        break;
    }
    address->Add(address_text);
    wxStaticText *port_text = new wxStaticText(this, wxID_ANY, ":" + std::to_string(record.port));
    port_text->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    port_text->SetForegroundColour(SERVER_PORT_WXCOLOR);
    address->Add(port_text, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(address, 1, wxEXPAND | wxBOTTOM, 5);

    wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
    wxButton *move_up_button = new wxButton(this, wxID_ANY, L"▲");
    move_up_button->Bind(wxEVT_BUTTON, &ServerWidget::OnMoveUp, this);
    buttons->Add(move_up_button, 0, wxRIGHT, 5);
    wxButton *move_down_button = new wxButton(this, wxID_ANY, L"▼");
    move_down_button->Bind(wxEVT_BUTTON, &ServerWidget::OnMoveDown, this);
    buttons->Add(move_down_button, 0, wxRIGHT, 5);
    wxButton *copy_button = new wxButton(this, wxID_ANY, L"📋Copy Address");
    copy_button->Bind(wxEVT_BUTTON, &ServerWidget::CopyAddress, this);
    buttons->Add(copy_button, 0, wxRIGHT, 5);
    buttons->Add(new wxPanel(this), 1, wxEXPAND);
    this->connect_button = new wxButton(this, wxID_ANY, "Connect");
    this->connect_button->Bind(wxEVT_BUTTON, &ServerWidget::OnConnect, this);
    buttons->Add(this->connect_button, 0, wxRIGHT, 5);
    wxButton *delete_button = new wxButton(this, wxID_ANY, "Delete");
    delete_button->Bind(wxEVT_BUTTON, &ServerWidget::OnDelete, this);
    buttons->Add(delete_button, 0);
    sizer->Add(buttons, 0, wxEXPAND);
    // sizer->Add(buttons, 0, wxEXPAND | wxALIGN_LEFT);

    padding->Add(sizer, 0, wxALL | wxEXPAND, 5);
    this->SetSizer(padding);
}

void select_all(wxKeyEvent &event)
{
    if (event.GetKeyCode() == 'A' && event.ControlDown())
    {
        wxTextCtrl *ctrl = dynamic_cast<wxTextCtrl *>(event.GetEventObject());
        if (ctrl)
        {
            ctrl->SetSelection(-1, -1);
            return; // Stop the event here
        }
    }
    event.Skip(); // Let other keys work normally
}

void focus_select_all(wxFocusEvent &event)
{
    event.Skip();
    wxTextCtrl *ctrl = wxDynamicCast(event.GetEventObject(), wxTextCtrl);
    if (ctrl)
    {
        ctrl->CallAfter([ctrl]()
                        { ctrl->SetSelection(-1, -1); });
    }
}

bool MyApp::OnInit()
{

#ifdef _WIN32
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed." << std::endl;
        wxMessageBox("Winsock initialization failed", "Error");
        return false;
    }
    this->wsaData = wsaData;
#endif
    int rc = sqlite3_open("servers.db", &this->db);
    if (rc != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        wxMessageBox("Couldn't create save file.", "Error");
        return false;
    }

    char *error_message = nullptr;
    rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS server(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, address_type INTEGER, address TEXT NOT NULL, port INTEGER);", nullptr, nullptr, &error_message);

    if (rc != SQLITE_OK)
    {
        std::cerr << "Table creation failed: " << error_message << std::endl;
        sqlite3_free(error_message);
        sqlite3_close(this->db);
        return false;
    }
    MainFrame *frame = new MainFrame(db);
    frame->Show(true);

    return true;
}

int MyApp::OnExit()
{
#ifdef _WIN32
    WSACleanup();
#endif

    sqlite3_close(this->db);
    return wxApp::OnExit();
}

void MainFrame::OnPortUpdate(wxFocusEvent &event)
{
    int new_port;
    wxTextCtrl *port_input = (wxTextCtrl *)event.GetEventObject();
    wxString port_wxstring = port_input->GetValue();
    if (port_wxstring.ToInt(&new_port))
    {
        if (new_port < 1 || new_port > 65535)
        {
            new_port = -1;
        }
    }
    else
    {
        new_port = -1;
    }
    // wxString str_port = event.GetString();
    // std::cout << new_port << std::endl;
    this->port = new_port;
    event.Skip();
}

MainFrame::MainFrame(sqlite3 *db)
    : wxFrame(NULL, wxID_ANY, "Hytale UDP Proxy", wxDefaultPosition, wxSize(800, 600))
{
    this->proxy_thread = nullptr;
    this->Bind(wxEVT_CONNECT_SERVER_RECORD, &MainFrame::ConnectFromServerRecord, this);
    this->Bind(wxEVT_DELETE_SERVER_RECORD, &MainFrame::DeleteServerRecord, this);
    this->Bind(wxEVT_MOVE_SERVER_RECORD, &MainFrame::MoveServerRecord, this);
    this->Bind(wxEVT_PROXY_THREAD_UPDATE, &MainFrame::OnProxyThreadUpdate, this);
    this->Bind(wxEVT_PROXY_THREAD_STOPPED, &MainFrame::OnProxyThreadStopped, this);
    this->Bind(wxEVT_PROXY_THREAD_RESOLVED_ADDRESS, &MainFrame::OnProxyThreadResolvedAddress, this);
    this->Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    this->db = db;
    this->port = PROXY_DEFAULT_PORT;
    this->SetMinSize(wxSize(800, 600));

    wxPanel *left_col = new wxPanel(this);
    left_col->SetCanFocus(false);
    // left_col->SetBackgroundColour(wxColor(255, 0, 0));
    wxBoxSizer *padding_sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *left_sizer = new wxBoxSizer(wxVERTICAL);
    padding_sizer->Add(left_sizer, 1, wxEXPAND | wxALL, 10);
    left_col->SetSizer(padding_sizer);
    // Right Column Content

    wxFlexGridSizer *port_setting = new wxFlexGridSizer(0, 2, 10, 10);
    // port_setting->AddGrowableCol(1, 2);
    // port_setting->AddGrowableCol(2, 4);
    port_setting->Add(new wxStaticText(left_col, wxID_ANY, "Proxy Port:"), 0, wxALIGN_CENTER_VERTICAL);
    std::string default_port = std::to_string(PROXY_DEFAULT_PORT);
    wxTextCtrl *port_input = new wxTextCtrl(left_col, wxID_ANY, default_port);
    port_input->SetMinSize(wxSize(50, wxDefaultCoord));
    port_input->SetMaxSize(wxSize(50, wxDefaultCoord));
    port_input->SetMaxLength(5);
    port_input->SetHint(default_port);
    port_input->SetCanFocus(false);
    port_input->SetValidator(wxTextValidator(wxFILTER_DIGITS));
    port_input->Bind(wxEVT_KILL_FOCUS, &MainFrame::OnPortUpdate, this);
    port_input->Bind(wxEVT_CHAR_HOOK, &select_all);
    this->ptr_port_input = port_input;
    port_setting->Add(port_input, 1, wxEXPAND);
    // port_setting->Add(new wxPanel(left_col), 1, wxEXPAND);
    left_sizer->Add(port_setting, 0, wxEXPAND | wxBOTTOM, 5);

    wxFlexGridSizer *ip_field = new wxFlexGridSizer(0, 3, 10, 10);
    ip_field->AddGrowableCol(1);
    ip_field->Add(new wxStaticText(left_col, wxID_ANY, "Server Address:"), 0, wxALIGN_CENTER_VERTICAL);

    wxFlexGridSizer *group_button = new wxFlexGridSizer(0, 2, 0, 0);
    group_button->AddGrowableCol(0);
    wxTextCtrl *ip_input = new wxTextCtrl(left_col, wxID_ANY, "");
    ip_input->Bind(wxEVT_CHAR_HOOK, &select_all);
    ip_input->SetHint("192.168.1.1:9520 or [2001:db8::1]:9520");
    ip_input->SetCanFocus(false);
    group_button->Add(ip_input, 1, wxEXPAND);
    this->ptr_ip_input = ip_input;
    // left_sizer->Add(ip_input, 1, wxEXPAND);

    wxButton *connect_button = new wxButton(left_col, wxID_ANY, "Connect");

    ptr_connect_button = connect_button;
    connect_button->Bind(wxEVT_BUTTON, &MainFrame::OnDirectConnect, this);
    group_button->Add(connect_button, 0, wxALIGN_CENTER_VERTICAL);
    ip_field->Add(group_button, 1, wxEXPAND);

    wxButton *save_button = new wxButton(left_col, wxID_ANY, "Save");
    save_button->Bind(wxEVT_BUTTON, &MainFrame::OnSave, this);
    ptr_save_button = save_button;
    ip_field->Add(save_button, 0, wxALIGN_CENTER_VERTICAL);
    left_sizer->Add(ip_field, 0, wxEXPAND);

    // main_grid->Add(left_col, 3, (wxALL ^ wxRIGHT) | wxEXPAND, 10);
    // main_grid->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
    // main_grid->Add(right_col, 1, (wxALL ^ wxLEFT) | wxEXPAND, 10);
    // main_grid->AddGrowableCol(0, 2);
    // main_grid->AddGrowableCol(2, 3);
    // main_grid->AddGrowableRow(0);
    this->server_list = new wxScrolledWindow(left_col, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxSUNKEN_BORDER);
    // this->server_list->SetBackgroundColour(wxColor(255, 0, 0));
    wxBoxSizer *server_sizer = new wxBoxSizer(wxVERTICAL);
    this->server_list->SetSizer(server_sizer);
    this->server_list->SetScrollRate(0, 10);
    left_sizer->Add(new wxStaticLine(left_col, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL), 0, wxEXPAND | wxTOP | wxBOTTOM, 5);
    left_sizer->Add(new wxStaticText(left_col, wxID_ANY, "Saved Servers:"), 0, wxBOTTOM, 5);
    left_sizer->Add(server_list, 1, wxEXPAND);

    left_sizer->Add(new wxStaticLine(left_col, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL), 0, wxEXPAND | wxTOP | wxBOTTOM, 5);
    // left_sizer->Add(new wxStaticText(left_col, wxID_ANY, "Saved Servers:"), 0, wxBOTTOM, 5);

    wxBoxSizer *proxy_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *first = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *temp = new wxBoxSizer(wxHORIZONTAL);
    temp->Add(new wxStaticText(left_col, wxID_ANY, "Profile:"), 0, wxRIGHT, 5);
    wxStaticText *profile_name = new wxStaticText(left_col, wxID_ANY, "N/A");
    wxFont profile_font = profile_name->GetFont();
    profile_font.SetWeight(wxFONTWEIGHT_BOLD);
    profile_name->SetFont(profile_font);
    this->profile_name_ptr = profile_name;
    temp->Add(profile_name);
    first->Add(temp, 1, wxEXPAND | wxRIGHT, 5);
    temp = new wxBoxSizer(wxHORIZONTAL);
    temp->Add(new wxStaticText(left_col, wxID_ANY, "Status:"), 0, wxRIGHT, 5);

    wxSimplebook *status_book = new wxSimplebook(left_col, wxID_ANY);

    // Stopped 0
    wxStaticText *status_text = new wxStaticText(status_book, wxID_ANY, "Stopped", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    status_text->SetForegroundColour(*wxRED);
    wxFont status_font = status_text->GetFont();
    status_font.SetWeight(wxFONTWEIGHT_BOLD);
    status_text->SetFont(status_font);
    status_book->AddPage(status_text, "Stopped");

    // Starting 1
    status_text = new wxStaticText(status_book, wxID_ANY, "Starting...", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    status_text->SetForegroundColour(*wxBLUE);
    status_font = status_text->GetFont();
    status_font.SetWeight(wxFONTWEIGHT_BOLD);
    status_text->SetFont(status_font);
    status_book->AddPage(status_text, "Starting");

    // Ready 2
    status_text = new wxStaticText(status_book, wxID_ANY, "Ready", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    status_text->SetForegroundColour(*wxGREEN);
    status_font = status_text->GetFont();
    status_font.SetWeight(wxFONTWEIGHT_BOLD);
    status_text->SetFont(status_font);
    status_book->AddPage(status_text, "Ready");

    // Connected 3
    status_text = new wxStaticText(status_book, wxID_ANY, "Connected", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    status_text->SetForegroundColour(*wxBLUE);
    status_font = status_text->GetFont();
    status_font.SetWeight(wxFONTWEIGHT_BOLD);
    status_text->SetFont(status_font);
    status_book->AddPage(status_text, "Connected");

    // Stopping 4
    status_text = new wxStaticText(status_book, wxID_ANY, "Stopping...", wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    status_text->SetForegroundColour(*wxRED);
    status_font = status_text->GetFont();
    status_font.SetWeight(wxFONTWEIGHT_BOLD);
    status_text->SetFont(status_font);
    status_book->AddPage(status_text, "Stopping");

    status_book->SetSelection(0);

    this->status_book = status_book;

    temp->Add(status_book, 1, wxEXPAND);
    first->Add(temp, 0, wxEXPAND);
    proxy_sizer->Add(first, 0, wxEXPAND | wxBOTTOM, 5);
    temp = new wxBoxSizer(wxHORIZONTAL);
    temp->Add(new wxStaticText(left_col, wxID_ANY, "Proxy Address:"), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
    wxTextCtrl *proxy_address = new wxTextCtrl(left_col, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    proxy_address->Bind(wxEVT_SET_FOCUS, &focus_select_all);
    profile_address_ptr = proxy_address;
    temp->Add(proxy_address, 1, wxEXPAND);
    wxButton *copy_proxy_address_button = new wxButton(left_col, wxID_ANY, "Copy");
    copy_proxy_address_button->Bind(wxEVT_BUTTON, &MainFrame::OnCopyProxyAddress, this);
    copy_proxy_address_button->Disable();
    this->copy_proxy_address_button_ptr = copy_proxy_address_button;
    temp->Add(copy_proxy_address_button);
    proxy_sizer->Add(temp, 0, wxEXPAND | wxBOTTOM, 5);
    wxBoxSizer *server_address_sizer = new wxBoxSizer(wxHORIZONTAL);
    temp = new wxBoxSizer(wxHORIZONTAL);
    temp->Add(new wxStaticText(left_col, wxID_ANY, "Server Address:"), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
    wxTextCtrl *server_txt_ctrol = new wxTextCtrl(left_col, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    server_txt_ctrol->Disable();
    this->proxy_server_address_ptr = server_txt_ctrol;
    temp->Add(server_txt_ctrol, 1, wxEXPAND);
    server_address_sizer->Add(temp, 1, wxEXPAND | wxRight, 5);
    temp = new wxBoxSizer(wxHORIZONTAL);
    temp->Add(new wxStaticText(left_col, wxID_ANY, "Resolved Address:"), 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, 5);
    wxTextCtrl *resolved_address = new wxTextCtrl(left_col, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    resolved_address->Disable();
    this->proxy_resolved_server_address_ptr = resolved_address;
    temp->Add(resolved_address, 2, wxEXPAND);
    server_address_sizer->Add(temp, 1, wxEXPAND);
    server_address_sizer->Hide(1);
    this->proxy_server_address_sizer_ptr = server_address_sizer;

    proxy_sizer->Add(server_address_sizer, 0, wxEXPAND | wxBOTTOM, 0);

    temp = new wxBoxSizer(wxHORIZONTAL);
    temp->Add(proxy_sizer, 1, wxEXPAND | wxRIGHT, 10);
    wxButton *stop = new wxButton(left_col, wxID_ANY, "Stop Proxy");
    stop->Bind(wxEVT_BUTTON, &MainFrame::OnStopProxy, this);
    stop->Disable();
    this->ptr_stop_proxy_button = stop;

    temp->Add(stop, 0, wxEXPAND);

    left_sizer->Add(temp, 0, wxEXPAND);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(left_col, 1, wxEXPAND);
    this->SetSizer(sizer);

    auto records = LoadServerRecordFromSql();
    for (const auto &server : records)
    {
        this->RenderServerRecord(server);
    }

    // Force the layout to calculate
    this->Layout();
}

void MainFrame::OnDirectConnect(wxCommandEvent &event)
{
    if (this->port == -1)
    {
        wxMessageBox(
            "Port should be in range [1-65535]",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        return;
    }

    wxString ip_wxstring = this->ptr_ip_input->GetValue().Trim(true).Trim(false);
    int proxy_port = this->port;
    auto [address_type, address, port] = resolve_server_address(ip_wxstring.ToStdString());
    if (MainFrame::ValidateServerAddress(address_type, address, port) != 0)
    {
        return;
    }

    ServerRecord record = {0, "", address_type, address, port};
    wxThreadEvent *request = new wxThreadEvent(wxEVT_CONNECT_SERVER_RECORD);
    request->SetEventObject(this);
    request->SetPayload(record);
    wxQueueEvent(this, request);
}

void MainFrame::OnSave(wxCommandEvent &event)
{
    wxString ip_wxstring = this->ptr_ip_input->GetValue().Trim(true).Trim(false);
    std::string ip_string = ip_wxstring.ToStdString();
    int proxy_port = this->port;
    auto [address_type, address, port] = resolve_server_address(ip_string);
    if (MainFrame::ValidateServerAddress(address_type, address, port) != 0)
    {
        return;
    }

    std::string server_name;

    while (true)
    {
        wxTextEntryDialog dialog(this, "Server Name:", "Save Server (" + ip_string + ")", "");

        wxTextCtrl *textCtrl = nullptr;
        wxWindowList &children = dialog.GetChildren();
        for (wxWindowList::iterator it = children.begin(); it != children.end(); ++it)
        {
            textCtrl = wxDynamicCast(*it, wxTextCtrl);
            if (textCtrl)
            {
                textCtrl->Bind(wxEVT_CHAR_HOOK, &select_all);
                break;
            }
        }

        if (dialog.ShowModal() != wxID_OK)
        {
            return;
        }

        wxString value = dialog.GetValue().Trim(true).Trim(false);
        if (value.IsEmpty())
        {
            wxMessageBox(
                "Name can't be empty.",
                "Save Server (" + ip_string + ")",
                wxOK | wxICON_ERROR);
            continue;
        }
        server_name = value.ToStdString();
        break;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    sqlite3_stmt *stmt;

    const char *sql = "INSERT INTO server (name,address_type,address,port) VALUES (?, ?, ?, ?) RETURNING id;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    sqlite3_bind_text(stmt, 1, server_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(address_type));
    sqlite3_bind_text(stmt, 3, address.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, port);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int newId = sqlite3_column_int(stmt, 0);
        sqlite3_step(stmt);
        sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
        sqlite3_finalize(stmt);
        this->RenderServerRecord({newId, server_name, address_type, address, port});
        return;
    }
    else
    {
        // Something went wrong, undo everything!
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        sqlite3_finalize(stmt);
    }
}

void MainFrame::RenderServerRecord(ServerRecord record)
{
    if (record.address_type == eAddressType::Invalid)
    {
        return;
    }

    wxBoxSizer *server_sizer = (wxBoxSizer *)this->server_list->GetSizer();
    ServerWidget *widget = new ServerWidget(this->server_list, record);

    server_sizer->Add(widget, 0, wxALL | wxEXPAND);

    server_sizer->Layout();
    this->server_list->FitInside();
}

void MainFrame::OnProxyThreadUpdate(wxThreadEvent &event)
{
    int state = event.GetInt();
    switch (state)
    {
    case 1:
        this->profile_address_ptr->SetValue(event.GetString());
        this->copy_proxy_address_button_ptr->Enable();
        this->status_book->SetSelection(2);
        break;
    case 2:
        this->status_book->SetSelection(3);
        break;

    default:
        break;
    }
    this->status_book->GetParent()->Layout();
}

// Reenabling
void MainFrame::OnProxyThreadStopped(wxThreadEvent &event)
{
    std::cout << "Proxy Stopped" << std::endl;
    this->ptr_port_input->Enable();
    this->ptr_ip_input->Enable();
    this->ptr_connect_button->Enable();
    this->ptr_save_button->Enable();
    this->ptr_stop_proxy_button->Disable();
    this->proxy_server_address_ptr->SetValue("");
    this->proxy_server_address_sizer_ptr->Hide(1);
    this->profile_name_ptr->SetLabel("N/A");
    this->profile_name_ptr->SetForegroundColour(wxColour());
    this->profile_address_ptr->SetValue("");
    this->copy_proxy_address_button_ptr->Disable();
    this->Layout();

    wxSizer *server_sizer = this->server_list->GetSizer();
    wxSizerItemList &children = server_sizer->GetChildren();

    for (auto child : children)
    {
        ServerWidget *widget = wxDynamicCast(child->GetWindow(), ServerWidget);

        if (widget)
        {
            widget->SetConnectButtonEnable(true);
        }
    }

    this->status_book->SetSelection(0);

    int state = event.GetInt();

    switch (state)
    {
    case 1:
        wxMessageBox(
            "Couldn't resolve domain \"" + event.GetString().ToStdString() + "\".",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        break;
    case 2:
        wxMessageBox(
            "For domain \"" + event.GetString().ToStdString() + "\", couldn't connect to any detected address.",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        break;
    case 3:
        wxMessageBox(
            "Failed to create socket.",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        break;
    case 4:
        wxMessageBox(
            "Failed to start on port " + event.GetString().ToStdString() + ".",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        break;
    case 5:
        wxMessageBox(
            "Unknown Operation Mode Error.",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        break;
    default:
        break;
    }
}

void MainFrame::OnProxyThreadResolvedAddress(wxThreadEvent &event)
{
    this->proxy_server_address_sizer_ptr->Show(1, true);
    this->proxy_resolved_server_address_ptr->SetValue(event.GetString());
    this->Layout();
}

void MainFrame::StopProxy()
{
    std::cout << "Called to stop proxy;" << std::endl;

    {
        wxCriticalSectionLocker enter(m_pThreadCS);

        if (this->proxy_thread) // does the thread still exist?
        {
            wxMessageOutputDebug().Printf("MYFRAME: deleting thread");

            if (this->proxy_thread->Delete() != wxTHREAD_NO_ERROR)
                wxLogError("Can't delete the thread!");
        }
    } // exit from the critical section to give the thread
      // the possibility to enter its destructor
      // (which is guarded with m_pThreadCS critical section!)

    while (1)
    {
        { // was the ~MyThread() function executed?
            wxCriticalSectionLocker enter(m_pThreadCS);
            if (!this->proxy_thread)
                break;
        }

        // wait for thread completion
        wxThread::This()->Sleep(1);
    }
}
void MainFrame::OnStopProxy(wxCommandEvent &event)
{
    this->ptr_stop_proxy_button->Disable();
    this->status_book->SetSelection(4);
    this->status_book->GetParent()->Layout();

    wxYield();
    wxMilliSleep(500);
    this->StopProxy();
}

void MainFrame::OnClose(wxCloseEvent &)
{
    this->StopProxy();

    this->Destroy();
}

void MainFrame::OnCopyProxyAddress(wxCommandEvent &event)
{
    if (wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(this->profile_address_ptr->GetValue()));
        wxTheClipboard->Close();
    }
}