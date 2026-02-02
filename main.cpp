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
class MainFrame : public wxFrame
{
public:
    MainFrame(sqlite3 *db);

private:
    sqlite3 *db;
    void OnPortUpdate(wxFocusEvent &event);
    wxTextCtrl *ptr_port_input;
    wxTextCtrl *ptr_ip_input;
    wxScrolledWindow *server_list;
    int port;
    wxButton buttons[2];
    void OnDirectConnect(wxCommandEvent &event);
    void OnSave(wxCommandEvent &event);
    void ConnectTo(int proxy_port, std::string domain, int port);
    void ConnectTo(int proxy_port, in_addr ipv4, int port);
    void ConnectTo(int proxy_port, in_addr6 ipv6, int port);
    void RenderServerRecord(ServerRecord record);

    int ValidateServerAddress(eAddressType address_type, std::string address, int port);
    std::vector<ServerRecord> LoadServerRecordFromSql();

    // Handle
    void MoveServerRecord(wxThreadEvent &event);
    void ConnectFromServerRecord(wxThreadEvent &event);
    void DeleteServerRecord(wxThreadEvent &event);
    int CreateProxySocket(int port);
};

class ServerWidget : public wxPanel
{
private:
    ServerRecord record;
    void OnConnect(wxCommandEvent &event);
    void OnDelete(wxCommandEvent &event);
    void OnMoveUp(wxCommandEvent &event);
    void OnMoveDown(wxCommandEvent &event);

public:
    ServerWidget(wxWindow *parent, ServerRecord record);
    int GetRecordId();
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

int ServerWidget::GetRecordId()
{
    return this->record.id;
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
    int move = event.GetInt();

    if (move == SERVER_MOVE_UP)
    {
    }
}

void ServerWidget::OnConnect(wxCommandEvent &event)
{
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
    buttons->Add(move_down_button, 0);
    buttons->Add(new wxPanel(this), 1, wxEXPAND);
    wxButton *connect_button = new wxButton(this, wxID_ANY, "Connect");
    connect_button->Bind(wxEVT_BUTTON, &ServerWidget::OnConnect, this);
    buttons->Add(connect_button, 0, wxRIGHT, 5);
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
    : wxFrame(NULL, wxID_ANY, "UDP Proxy", wxDefaultPosition, wxSize(800, 600))
{
    // this->Bind(wxEVT_CONNECT_SERVER_RECORD, &MainFrame::ConnectFromServerRecord, this);
    this->Bind(wxEVT_DELETE_SERVER_RECORD, &MainFrame::DeleteServerRecord, this);
    this->Bind(wxEVT_MOVE_SERVER_RECORD, &MainFrame::MoveServerRecord, this);
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

    wxFlexGridSizer *port_setting = new wxFlexGridSizer(0, 3, 10, 10);
    port_setting->AddGrowableCol(1, 2);
    port_setting->AddGrowableCol(2, 4);
    port_setting->Add(new wxStaticText(left_col, wxID_ANY, "Proxy Port:"), 0, wxALIGN_CENTER_VERTICAL);
    std::string default_port = std::to_string(PROXY_DEFAULT_PORT);
    wxTextCtrl *port_input = new wxTextCtrl(left_col, wxID_ANY, default_port);
    port_input->SetHint(default_port);
    port_input->SetCanFocus(false);
    port_input->SetValidator(wxTextValidator(wxFILTER_DIGITS));
    port_input->Bind(wxEVT_KILL_FOCUS, &MainFrame::OnPortUpdate, this);
    port_input->Bind(wxEVT_CHAR_HOOK, &select_all);
    this->ptr_port_input = port_input;
    port_setting->Add(port_input, 1, wxEXPAND);
    port_setting->Add(new wxPanel(left_col), 1, wxEXPAND);
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
    connect_button->Bind(wxEVT_BUTTON, &MainFrame::OnDirectConnect, this);
    group_button->Add(connect_button, 0, wxALIGN_CENTER_VERTICAL);
    ip_field->Add(group_button, 1, wxEXPAND);

    wxButton *save_button = new wxButton(left_col, wxID_ANY, "Save");
    save_button->Bind(wxEVT_BUTTON, &MainFrame::OnSave, this);
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

void MainFrame::ConnectTo(int proxy_port, std::string domain, int port)
{
}

void MainFrame::ConnectTo(int proxy_port, in_addr ipv4, int port)
{
    int proxy_socket = MainFrame::CreateProxySocket(proxy_port);
    if (proxy_socket < 0)
    {
        return;
    }
    IPv4Proxy proxy(proxy_socket);
    proxy.connect(ipv4, port);
    close_socket(proxy_socket);
}

void MainFrame::ConnectTo(int proxy_port, in_addr6 ipv6, int port)
{
}

int MainFrame::CreateProxySocket(int port)
{
    int proxySocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (proxySocket < 0)
    {
        perror("Proxy socket creation failed");
        wxMessageBox(
            "Failed to make socket",
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        return proxySocket;
    }
    sockaddr_in proxyAddress;
    proxyAddress.sin_family = AF_INET;
    proxyAddress.sin_port = htons(port);
    proxyAddress.sin_addr.s_addr = INADDR_ANY;
    if (bind(proxySocket, (struct sockaddr *)&proxyAddress, sizeof(proxyAddress)) == -1)
    {
        perror("Proxy bind failed");
        close_socket(proxySocket);
        wxMessageBox(
            "Failed to start on port " + std::to_string(port),
            "Proxy Server Error",
            wxOK | wxICON_ERROR);
        return -1;
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
    return proxySocket;
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
    switch (address_type)
    {
    case IPv4:
        in_addr ipv4;
        inet_pton(AF_INET, address.c_str(), &ipv4);
        std::cout << test_ipv4_quic(ipv4, port) << std::endl;
        break;
    case IPv6:
        in6_addr ipv6;
        inet_pton(AF_INET6, address.c_str(), &ipv6);
        std::cout << test_ipv6_quic(ipv6, port) << std::endl;
        /* code */
        break;
    case Domain:
        /* code */
        break;
    default:
        break;
    }
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
    ServerWidget *widget = new ServerWidget(this->server_list, record);
    wxBoxSizer *server_sizer = (wxBoxSizer *)this->server_list->GetSizer();

    server_sizer->Add(widget, 0, wxALL | wxEXPAND);

    server_sizer->Layout();
    this->server_list->FitInside();
}