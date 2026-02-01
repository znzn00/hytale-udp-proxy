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

class MyApp : public wxApp
{
    sqlite3 *db;
    WSAData wsaData;

public:
    virtual bool OnInit();
    virtual int OnExit() override;
};

class MainFrame : public wxFrame
{
public:
    MainFrame(sqlite3 *db);

private:
    sqlite3 *db;
    void OnPortUpdate(wxFocusEvent &event);
    wxTextCtrl *ptr_port_input;
    wxTextCtrl *ptr_ip_input;
    int port;
    wxButton buttons[2];

    void OnDirectConnect(wxCommandEvent &event);
    void ConnectTo(int proxy_port, std::string domain, int port);
    void ConnectTo(int proxy_port, in_addr ipv4, int port);
    void ConnectTo(int proxy_port, in_addr6 ipv6, int port);
    int CreateProxySocket(int port);
    // void OnHello(wxCommandEvent &event);
    // void OnExit(wxCommandEvent &event);
    // void OnAbout(wxCommandEvent &event);
};

wxIMPLEMENT_APP(MyApp);

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

    char *errorMessage = nullptr;
    int rc = sqlite3_exec(db, "", nullptr, nullptr, &errorMessage);

    if (rc != SQLITE_OK)
    {
        std::cerr << "Table creation failed: " << errorMessage << std::endl;
        sqlite3_free(errorMessage);
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
    ip_field->Add(save_button, 0, wxALIGN_CENTER_VERTICAL);
    left_sizer->Add(ip_field, 0, wxEXPAND);

    // main_grid->Add(left_col, 3, (wxALL ^ wxRIGHT) | wxEXPAND, 10);
    // main_grid->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
    // main_grid->Add(right_col, 1, (wxALL ^ wxLEFT) | wxEXPAND, 10);
    // main_grid->AddGrowableCol(0, 2);
    // main_grid->AddGrowableCol(2, 3);
    // main_grid->AddGrowableRow(0);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(left_col, 1, wxEXPAND);
    this->SetSizer(sizer);

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

    wxString ip_wxstring = this->ptr_ip_input->GetValue();
    int proxy_port = this->port;
    auto [address_type, address, port] = resolve_server_address(ip_wxstring.ToStdString());
    std::cout << address << " | " << port << std::endl;
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