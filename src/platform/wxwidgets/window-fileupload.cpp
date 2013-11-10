#include "platform/wxwidgets/platform.hpp"
#include "platform/wxwidgets/menu_upload.hpp"
#include "core/fileupload.hpp"
#include "core/misc.hpp"
#include "core/rom.hpp"
#include "core/project.hpp"
#include "core/moviedata.hpp"
#include "library/skein.hpp"
#include "library/zip.hpp"
#include "library/json.hpp"
#include "library/string.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <wx/wx.h>
#include <wx/event.h>
#include <wx/control.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/stream_buffer.hpp>
#include <boost/iostreams/filter/symmetric.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>

std::string pick_file(wxWindow* parent, const std::string& title, const std::string& startdir);

namespace
{
class wxeditor_uploadtarget : public wxDialog
{
public:
	wxeditor_uploadtarget(wxWindow* parent);
	wxeditor_uploadtarget(wxWindow* parent, upload_menu::upload_entry entry);
	upload_menu::upload_entry get_entry();
	void generate_dh25519(wxCommandEvent& e);
	void on_ok(wxCommandEvent& e);
	void on_cancel(wxCommandEvent& e);
	void on_auth_sel(wxCommandEvent& e);
	void revalidate(wxCommandEvent& e);
private:
	void dh25519_fill_box();
	void ctor_common();
	wxButton* ok;
	wxButton* cancel;
	wxTextCtrl* name;
	wxTextCtrl* url;
	wxComboBox* auth;
	wxPanel* dh25519_p;
	wxTextCtrl* dh25519_k;
	wxButton* dh25519_g;
};

wxeditor_uploadtarget::wxeditor_uploadtarget(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, towxstring("lsnes: New upload target"))
{
	ctor_common();
	wxCommandEvent e;
	on_auth_sel(e);
}

wxeditor_uploadtarget::wxeditor_uploadtarget(wxWindow* parent, upload_menu::upload_entry entry)
	: wxDialog(parent, wxID_ANY, towxstring("lsnes: Edit upload target: " + entry.name))
{
	ctor_common();
	name->SetValue(towxstring(entry.name));
	url->SetValue(towxstring(entry.url));
	switch(entry.auth) {
	case upload_menu::upload_entry::AUTH_DH25519:
		auth->SetSelection(0);
		wxCommandEvent e;
		on_auth_sel(e);
		break;
	}
}

void wxeditor_uploadtarget::dh25519_fill_box()
{
	try {
		uint8_t rbuf[32];
		get_dh25519_pubkey(rbuf);
		char out[65];
		out[64] = 0;
		for(unsigned i = 0; i < 32; i++)
			sprintf(out + 2 * i, "%02x", rbuf[i]);
		dh25519_k->SetValue(towxstring(out));
		dh25519_g->Disable();
	} catch(...) {
		dh25519_k->SetValue(towxstring("(Not available)"));
		dh25519_g->Enable();
	}
	wxCommandEvent e;
	revalidate(e);
}

void wxeditor_uploadtarget::ctor_common()
{
	ok = NULL;
	std::vector<wxString> auth_choices;
	Center();
	wxBoxSizer* top_s = new wxBoxSizer(wxVERTICAL);
	SetSizer(top_s);

	top_s->Add(new wxStaticText(this, wxID_ANY, towxstring("Name")), 0, wxGROW);
	top_s->Add(name = new wxTextCtrl(this, wxID_ANY, towxstring(""), wxDefaultPosition, wxSize(550, -1)), 0,
		wxGROW);
	top_s->Add(new wxStaticText(this, wxID_ANY, towxstring("URL")), 0, wxGROW);
	top_s->Add(url = new wxTextCtrl(this, wxID_ANY, towxstring(""), wxDefaultPosition, wxSize(550, -1)), 0,
		wxGROW);
	name->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(wxeditor_uploadtarget::revalidate), NULL,
		this);
	url->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(wxeditor_uploadtarget::revalidate), NULL,
		this);

	top_s->Add(new wxStaticText(this, wxID_ANY, towxstring("Authentication")), 0, wxGROW);
	auth_choices.push_back(towxstring("dh25519"));
	top_s->Add(auth = new wxComboBox(this, wxID_ANY, auth_choices[0], wxDefaultPosition, wxDefaultSize,
		auth_choices.size(), &auth_choices[0], wxCB_READONLY), 0, wxGROW);

	dh25519_p = new wxPanel(this, wxID_ANY);
	wxBoxSizer* dh25519_s = new wxBoxSizer(wxVERTICAL);
	dh25519_p->SetSizer(dh25519_s);
	wxStaticBox* dh25519_b = new wxStaticBox(dh25519_p, wxID_ANY, towxstring("Authentication parameters"));
	wxStaticBoxSizer* dh25519_s2 = new wxStaticBoxSizer(dh25519_b, wxVERTICAL);
	top_s->Add(dh25519_p, 0, wxGROW);
	dh25519_s->Add(dh25519_s2, 0, wxGROW);
	dh25519_s2->Add(new wxStaticText(dh25519_p, wxID_ANY, towxstring("Key")), 0, wxGROW);
	dh25519_s2->Add(dh25519_k = new wxTextCtrl(dh25519_p, wxID_ANY, towxstring(""), wxDefaultPosition,
		wxSize(550, -1), wxTE_READONLY), 0, wxGROW);
	dh25519_s2->Add(dh25519_g = new wxButton(dh25519_p, wxID_ANY, towxstring("Generate")), 0, wxGROW);
	dh25519_s->SetSizeHints(dh25519_p);
	dh25519_fill_box();

	wxBoxSizer* pbutton_s = new wxBoxSizer(wxHORIZONTAL);
	pbutton_s->AddStretchSpacer();
	pbutton_s->Add(ok = new wxButton(this, wxID_OK, wxT("OK")), 0, wxGROW);
	pbutton_s->Add(cancel = new wxButton(this, wxID_CANCEL, wxT("Cancel")), 0, wxGROW);
	ok->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploadtarget::on_ok), NULL, this);
	cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploadtarget::on_cancel), NULL, this);
	top_s->Add(pbutton_s, 0, wxGROW);

	wxCommandEvent e;
	revalidate(e);

	top_s->SetSizeHints(this);
	Fit();
}

void wxeditor_uploadtarget::on_ok(wxCommandEvent& e)
{
	EndModal(wxID_OK);
}

void wxeditor_uploadtarget::on_cancel(wxCommandEvent& e)
{
	EndModal(wxID_CANCEL);
}

void wxeditor_uploadtarget::generate_dh25519(wxCommandEvent& e)
{
	try {
		std::string entropy = pick_text(this, "Enter garbage", "Mash some garbage from keyboard to derive\n"
			"key from:", "", true);
		uint8_t rbuf[192];
		highrandom_256(rbuf + 0);
		highrandom_256(rbuf + 32);
		std::vector<char> x;
		x.resize(entropy.length());
		std::copy(entropy.begin(), entropy.end(), x.begin());
		skein_hash h(skein_hash::PIPE_1024, 1024);
		h.write((uint8_t*)&x[0], x.size());
		h.read((uint8_t*)rbuf + 64);
		{
			std::ofstream fp(get_config_path() + "/dh25519.key", std::ios::binary);
			if(!fp) throw std::runtime_error("Can't open keyfile");
#if !defined(_WIN32) && !defined(_WIN64)
			chmod((get_config_path() + "/dh25519.key").c_str(), 0600);
#endif
			fp.write((char*)rbuf, 192);
			if(!fp) throw std::runtime_error("Can't write keyfile");
		}
		dh25519_fill_box();
	} catch(canceled_exception& e) {
		return;
	} catch(std::exception& e) {
		show_message_ok(this, "Generate keys error", std::string("Error generating keys:") + e.what(),
			wxICON_EXCLAMATION);
		return;
	}
}

void wxeditor_uploadtarget::on_auth_sel(wxCommandEvent& e)
{
	dh25519_p->Show(false);
	switch(auth->GetSelection()) {
	case 0:
		dh25519_p->Show(true);
		break;
	}
	revalidate(e);
	Fit();
}

upload_menu::upload_entry wxeditor_uploadtarget::get_entry()
{
	upload_menu::upload_entry ent;
	ent.name = tostdstring(name->GetValue());
	ent.url = tostdstring(url->GetValue());
	ent.auth = upload_menu::upload_entry::AUTH_DH25519;
	switch(auth->GetSelection()) {
	case 0:
		ent.auth = upload_menu::upload_entry::AUTH_DH25519;
		break;
	}
	return ent;
}

void wxeditor_uploadtarget::revalidate(wxCommandEvent& e)
{
	bool valid = true;
	if(!name || (name->GetValue().Length() == 0)) valid = false;
	std::string URL = url ? tostdstring(url->GetValue()) : "";
	if(!regex_match("https?://(([!$&'()*+,;=:A-Za-z0-9._~-]|%[0-9A-Fa-f][0-9A-Fa-f])+|\\[v[0-9A-Fa-f]\\."
		"([!$&'()*+,;=:A-Za-z0-9._~-]|%[0-9A-Fa-f][0-9A-Fa-f])+\\]|\\[[0-9A-Fa-f:]+\\])"
		"(/([!$&'()*+,;=:A-Za-z0-9._~@-]|%[0-9A-Fa-f][0-9A-Fa-f])+)*", URL)) valid = false;
	if(!auth || (auth->GetSelection() == 0 && dh25519_g->IsEnabled())) valid = false;
	if(ok) ok->Enable(valid);
}

class wxeditor_uploadtargets : public wxDialog
{
public:
	wxeditor_uploadtargets(wxWindow* parent, upload_menu* menu);
	void on_ok(wxCommandEvent& e);
	void on_add(wxCommandEvent& e);
	void on_modify(wxCommandEvent& e);
	void on_remove(wxCommandEvent& e);
	void on_list_sel(wxCommandEvent& e);
private:
	void refresh();
	upload_menu* umenu;
	std::map<int, unsigned> id_map;
	wxListBox* list;
	wxButton* ok;
	wxButton* add;
	wxButton* modify;
	wxButton* _delete;
};

wxeditor_uploadtargets::wxeditor_uploadtargets(wxWindow* parent, upload_menu* menu)
	: wxDialog(parent, wxID_ANY, towxstring("lsnes: Configure upload targets"), wxDefaultPosition,
	wxSize(400, 500))
{
	umenu = menu;
	Center();
	wxBoxSizer* top_s = new wxBoxSizer(wxVERTICAL);
	SetSizer(top_s);

	top_s->Add(list = new wxListBox(this, wxID_ANY), 1, wxGROW);
	list->Connect(wxEVT_COMMAND_LISTBOX_SELECTED, 
		wxCommandEventHandler(wxeditor_uploadtargets::on_list_sel), NULL, this);

	wxBoxSizer* pbutton_s = new wxBoxSizer(wxHORIZONTAL);
	pbutton_s->Add(ok = new wxButton(this, wxID_OK, wxT("OK")), 0, wxGROW);
	pbutton_s->AddStretchSpacer();
	pbutton_s->Add(add = new wxButton(this, wxID_ADD, wxT("Add")), 0, wxGROW);
	pbutton_s->Add(modify = new wxButton(this, wxID_EDIT, wxT("Modify")), 0, wxGROW);
	pbutton_s->Add(_delete = new wxButton(this, wxID_DELETE, wxT("Delete")), 0, wxGROW);
	modify->Enable(false);
	_delete->Enable(false);
	ok->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploadtargets::on_ok), NULL, this);
	add->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploadtargets::on_add), NULL, this);
	modify->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploadtargets::on_modify), NULL, this);
	_delete->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploadtargets::on_remove), NULL, this);
	top_s->Add(pbutton_s, 0, wxGROW);

	refresh();
	top_s->SetSizeHints(this);
	Fit();
}

void wxeditor_uploadtargets::on_ok(wxCommandEvent& e)
{
	EndModal(wxID_OK);
}

void wxeditor_uploadtargets::on_add(wxCommandEvent& e)
{
	auto f = new wxeditor_uploadtarget(this);
	int r = f->ShowModal();
	if(r == wxID_OK) {
		unsigned ent = 0;
		auto used = umenu->entries();
		while(used.count(ent)) ent++;
		umenu->configure_entry(ent, f->get_entry());
	}
	f->Destroy();
	refresh();
}

void wxeditor_uploadtargets::on_modify(wxCommandEvent& e)
{
	auto s = list->GetSelection();
	if(s == wxNOT_FOUND) return;
	if(!id_map.count(s)) return;
	auto f = new wxeditor_uploadtarget(this, umenu->get_entry(id_map[s]));
	int r = f->ShowModal();
	if(r == wxID_OK)
		umenu->configure_entry(id_map[s], f->get_entry());
	f->Destroy();
	refresh();
}

void wxeditor_uploadtargets::on_remove(wxCommandEvent& e)
{
	auto s = list->GetSelection();
	if(s == wxNOT_FOUND) return;
	if(!id_map.count(s)) return;
	unsigned id = id_map[s];
	umenu->delete_entry(id);
	refresh();
}

void wxeditor_uploadtargets::on_list_sel(wxCommandEvent& e)
{
	auto s = list->GetSelection();
	modify->Enable(s != wxNOT_FOUND);
	_delete->Enable(s != wxNOT_FOUND);
}

void wxeditor_uploadtargets::refresh()
{
	auto ents = umenu->entries();
	auto sel = list->GetSelection();
	auto sel_id = id_map.count(sel) ? id_map[sel] : 0xFFFFFFFFU;

	list->Clear();
	id_map.clear();
	int num = 0;
	for(auto i : ents) {
		auto ent = umenu->get_entry(i);
		list->Append(towxstring(ent.name));
		id_map[num++] = i;
	}

	//Try to keep selection.
	if(sel_id != 0xFFFFFFFFU) {
		int x = wxNOT_FOUND;
		for(auto i : id_map)
			if(i.second == sel_id)
				x = i.first;
		if(x != wxNOT_FOUND)
			list->SetSelection(x);
	} else if(sel < list->GetCount())
		list->SetSelection(sel);
}

class wxeditor_uploaddialog : public wxDialog
{
public:
	wxeditor_uploaddialog(wxWindow* parent, upload_menu::upload_entry entry);
	void on_ok(wxCommandEvent& e);
	void on_cancel(wxCommandEvent& e);
	void on_source_sel(wxCommandEvent& e);
	void on_file_sel(wxCommandEvent& e);
	void timer_tick();
private:
	struct _timer : public wxTimer
	{
		_timer(wxeditor_uploaddialog* _dialog) { dialog = _dialog; start(); }
		void start() { Start(500); }
		void stop() { Stop(); }
		void Notify()
		{
			dialog->timer_tick();
		}
		wxeditor_uploaddialog* dialog;
	}* timer;
	struct _games_output_handler : public http_request::output_handler {
		~_games_output_handler()
		{
		}
		void header(const std::string& name, const std::string& cotent)
		{
			//No-op.
		}
		void write(const char* source, size_t srcsize)
		{
			std::string x(source, srcsize);
			while(x.find_first_of("\n") < x.length()) {
				size_t split = x.find_first_of("\n");
				std::string line = x.substr(0, split);
				x = x.substr(split + 1);
				incomplete_line += line;
				while(incomplete_line.length() > 0 &&
					incomplete_line[incomplete_line.length() - 1] == '\r')
					incomplete_line = incomplete_line.substr(0, incomplete_line.length() - 1);
				choices.insert(incomplete_line);
				incomplete_line = "";
			}
			if(x != "") incomplete_line += x;
			
		}
		void flush()
		{
			if(incomplete_line != "") choices.insert(incomplete_line);
		}
		std::string incomplete_line;
		std::set<std::string> choices;
	} games_output_handler;
	wxTextCtrl* status;
	wxTextCtrl* filename;
	wxTextCtrl* title;
	wxTextCtrl* description;
	wxComboBox* game;
	wxRadioButton* current;
	wxRadioButton* file;
	wxTextCtrl* ufilename;
	wxButton* file_select;
	wxGauge* progress;
	wxCheckBox* hidden;
	wxButton* ok;
	wxButton* cancel;
	file_upload* upload;
	http_async_request* games_req;
	upload_menu::upload_entry _entry;
};

wxeditor_uploaddialog::wxeditor_uploaddialog(wxWindow* parent, upload_menu::upload_entry entry)
	: wxDialog(parent, wxID_ANY, towxstring("lsnes: Upload file: " + entry.name), wxDefaultPosition,
		wxSize(-1, -1))
{
	_entry = entry;
	upload = NULL;
	Centre();
	wxBoxSizer* top_s = new wxBoxSizer(wxVERTICAL);
	SetSizer(top_s);

	top_s->Add(new wxStaticText(this, wxID_ANY, wxT("Filename:")), 0, wxGROW);
	top_s->Add(filename = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(550, -1)), 0,
		wxGROW);
	top_s->Add(new wxStaticText(this, wxID_ANY, wxT("Title:")), 0, wxGROW);
	top_s->Add(title = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(550, -1)), 0, wxGROW);
	top_s->Add(new wxStaticText(this, wxID_ANY, wxT("Description:")), 0, wxGROW);
	top_s->Add(description = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(550, 300),
		wxTE_MULTILINE), 0, wxGROW);
	top_s->Add(new wxStaticText(this, wxID_ANY, wxT("Game:")), 0, wxGROW);
	top_s->Add(game = new wxComboBox(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, NULL,
		wxCB_READONLY), 0, wxGROW);
	game->Append(towxstring("(default)"));
	game->SetSelection(0);
	top_s->Add(hidden = new wxCheckBox(this, wxID_ANY, wxT("Hidden")), 0, wxGROW);
	
	top_s->Add(current = new wxRadioButton(this, wxID_ANY, wxT("Current movie"), wxDefaultPosition, wxDefaultSize,
		wxRB_GROUP), 0, wxGROW);
	top_s->Add(file = new wxRadioButton(this, wxID_ANY, wxT("Specified file:")), 0, wxGROW);
	current->Connect(wxEVT_COMMAND_RADIOBUTTON_SELECTED,
		wxCommandEventHandler(wxeditor_uploaddialog::on_source_sel), NULL, this);
	file->Connect(wxEVT_COMMAND_RADIOBUTTON_SELECTED,
		wxCommandEventHandler(wxeditor_uploaddialog::on_source_sel), NULL, this);
	if(!our_rom.rtype || our_rom.rtype->isnull()) {
		current->Enable(false);
		file->SetValue(true);
	}

	wxBoxSizer* file_s = new wxBoxSizer(wxHORIZONTAL);
	file_s->Add(ufilename = new wxTextCtrl(this, wxID_ANY, wxT("")), 1, wxGROW);
	file_s->Add(file_select = new wxButton(this, wxID_ANY, wxT("...")), 0, wxGROW);
	top_s->Add(file_s, 0, wxGROW);
	file_select->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploaddialog::on_file_sel), NULL, this);
	ufilename->Enable(file->GetValue());
	file_select->Enable(file->GetValue());

	top_s->Add(status = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(550, 300),
		wxTE_READONLY | wxTE_MULTILINE), 0, wxGROW);
	top_s->Add(progress = new wxGauge(this, wxID_ANY, 1000000, wxDefaultPosition, wxSize(-1, 15),
		wxGA_HORIZONTAL), 0, wxGROW);

	status->AppendText(wxT("Obtaining list of games...\n"));
	games_req = new http_async_request();
	games_req->verb = "GET";
	games_req->url = entry.url + "/games";
	games_req->ihandler = NULL;
	games_req->ohandler = &games_output_handler;
	games_req->lauch_async();

	timer = new _timer(this);

	wxBoxSizer* pbutton_s = new wxBoxSizer(wxHORIZONTAL);
	pbutton_s->AddStretchSpacer();
	pbutton_s->Add(ok = new wxButton(this, wxID_OK, wxT("Upload")), 0, wxGROW);
	ok->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploaddialog::on_ok), NULL, this);
	pbutton_s->Add(cancel = new wxButton(this, wxID_OK, wxT("Cancel")), 0, wxGROW);
	cancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
		wxCommandEventHandler(wxeditor_uploaddialog::on_cancel), NULL, this);
	top_s->Add(pbutton_s, 0, wxGROW);
	top_s->SetSizeHints(this);
	Fit();
}

void wxeditor_uploaddialog::timer_tick()
{
	if(upload) {
		for(auto i : upload->get_messages()) {
			status->AppendText(towxstring(i + "\n"));
		}
		if(upload->finished) {
			delete upload;
			upload = NULL;
			cancel->SetLabel(wxT("Close"));
		} else {
			auto prog = upload->get_progress_ppm();
			if(prog < 0)
				progress->Pulse();
			else {
				progress->SetRange(1000000);
				progress->SetValue(prog);
			}
		}
	} else if(games_req) {
		progress->Pulse();
		if(games_req->finished) {
			std::string msg;
			games_output_handler.flush();
			if(games_req->errormsg != "") {
				msg = (stringfmt() << "Error getting list of games: " << (games_req->errormsg)).str();
			} else if(games_req->http_code != 200) {
				msg = (stringfmt() << "Got unexpected HTTP status " << (games_req->http_code)).str(); 
			} else {
				for(auto i : games_output_handler.choices)
					game->Append(towxstring(i));
				msg = "Got list of games.";
			}
			status->AppendText(towxstring(msg + "\n"));
			delete games_req;
			games_req = NULL;
			wxCommandEvent e;
			on_source_sel(e);
		}
	} else {
		if(progress) {
			progress->SetRange(1000000);
			progress->SetValue(0);
		}
	}
}

void wxeditor_uploaddialog::on_ok(wxCommandEvent& e)
{
	if(file->GetValue() && ufilename->GetValue().Length() == 0) return;
	std::string fn = filename->GetValue();
	std::vector<char> content;
	if(file->GetValue()) {
		if(fn == "")
			filename->SetValue(ufilename->GetValue());
		boost::iostreams::back_insert_device<std::vector<char>> rd(content);
		std::ifstream in(tostdstring(ufilename->GetValue()), std::ios::binary);
		if(!in) {
			status->AppendText("Can't open '" + tostdstring(ufilename->GetValue()) + "'\n");
			return;
		}
		boost::iostreams::copy(in, rd);
	} else {
		if(fn.length() < 6 || fn.substr(fn.length() - 5) != ".lsmv")
			filename->SetValue(fn + ".lsmv");
		our_movie.is_savestate = false;
		our_movie.input = movb.get_movie().save();
		auto prj = project_get();
		if(prj) {
			our_movie.gamename = prj->gamename;
			our_movie.authors = prj->authors;
		}
		our_movie.active_macros.clear();
		std::ostringstream stream;
		our_movie.save(stream);
		std::string _stream = stream.str();
		content = std::vector<char>(_stream.begin(), _stream.end());
	}
	ok->Enable(false);
	upload = new file_upload();
	upload->base_url = _entry.url;
	upload->content = content;
	upload->filename = tostdstring(filename->GetValue());
	upload->title = tostdstring(title->GetValue());
	upload->description = tostdstring(description->GetValue());
	upload->gamename = tostdstring(game->GetValue());
	upload->hidden = hidden->GetValue();
	upload->do_async();
}

void wxeditor_uploaddialog::on_source_sel(wxCommandEvent& e)
{
	ufilename->Enable(file->GetValue());
	file_select->Enable(file->GetValue());
	if(!games_req) {
		if(current->GetValue()) {
			std::string curgame;
			auto prj = project_get();
			if(prj)
				curgame = prj->gamename;
			else
				curgame = our_movie.gamename;

			std::string plat = lookup_sysregion_mapping(our_movie.gametype->get_name()) + " ";
			size_t platlen = plat.length();
			std::string c = tostdstring(game->GetValue());
			game->Clear();
			game->Append("(default)");
			std::string c2;
			std::string c3;
			for(auto i : games_output_handler.choices) {
				std::string j = i;
				if(j.substr(0, platlen) == plat) {
					game->Append(towxstring(i));
					if(j == c)
						c2 = c;
					if(j.substr(platlen) == curgame)
						c3 = j;
				}
			}
			game->SetSelection(0);
			if(c3 != "")
				game->SetValue(towxstring(c3));
			else if(c2 != "")
				game->SetValue(towxstring(c2));
		} else {
			std::string c = tostdstring(game->GetValue());
			game->Clear();
			game->Append("(default)");
			for(auto i : games_output_handler.choices)
				game->Append(towxstring(i));
			game->SetValue(towxstring(c));
		}
	}
}

void wxeditor_uploaddialog::on_file_sel(wxCommandEvent& e)
{
	std::string f;
	try {
		f = pick_file(this, "Pick file to send", ".");
	} catch(canceled_exception& e) {
		return;
	}
	ufilename->SetValue(towxstring(f));
}

void wxeditor_uploaddialog::on_cancel(wxCommandEvent& e)
{
	if(games_req) {
		games_req->cancel();
		while(!games_req->finished)
			usleep(100000);
		delete games_req;
	}
	if(upload) {
		upload->cancel();
		while(!upload->finished)
			usleep(100000);
		delete upload;
	}
	timer->stop();
	delete timer;
	EndModal(wxID_CANCEL);
}

}

upload_menu::upload_menu(wxWindow* win, int wxid_low, int wxid_high)
{
	pwin = win;
	wxid_range_low = wxid_low;
	wxid_range_high = wxid_high;
	Append(wxid_range_high, towxstring("Configure..."));
	win->Connect(wxid_low, wxid_high, wxEVT_COMMAND_MENU_SELECTED,
		wxCommandEventHandler(upload_menu::on_select), NULL, this);
	{
		std::ifstream in(get_config_path() + "/upload.cfg");
		std::string line;
		unsigned num = 0;
		while(std::getline(in, line)) {
			upload_entry entry;
			try {
				JSON::node n(line);
				if(!n.field_exists("name") || n.type_of("name") != JSON::string)
					continue;
				if(!n.field_exists("url") || n.type_of("url") != JSON::string)
					continue;
				if(!n.field_exists("auth") || n.type_of("auth") != JSON::string)
					continue;
				entry.name = n["name"].as_string8();
				entry.url = n["url"].as_string8();
				std::string auth = n["auth"].as_string8();
				if(auth == "dh25519")
					entry.auth = upload_entry::AUTH_DH25519;
				else
					continue;
			} catch(...) {
				continue;
			}
			if(num == 0)
				PrependSeparator();
			entry.item = Prepend(wxid_range_low + num, towxstring(entry.name + "...")); 
			destinations[wxid_range_low + num] = entry;
			num++;
		}
	}
}

upload_menu::~upload_menu()
{
}

void upload_menu::save()
{
	std::string base = get_config_path() + "/upload.cfg";
	{
		std::ofstream out(base + ".tmp");
		if(!out)
			return;
		for(auto i : destinations) {
			upload_entry entry = i.second;
			JSON::node n(JSON::object);
			n["name"] = JSON::string(entry.name);
			n["url"] = JSON::string(entry.url);
			switch(entry.auth) {
			case upload_entry::AUTH_DH25519:
				n["auth"] = JSON::string("dh25519");
				break;
			}
			out << n.serialize() << std::endl;
		}
		if(!out)
			return;
	}
	rename_file_overwrite((base + ".tmp").c_str(), base.c_str());
}

void upload_menu::configure_entry(unsigned num, struct upload_entry entry)
{
	if(destinations.count(wxid_range_low + num)) {
		//Reconfigure.
		auto tmp = destinations[wxid_range_low + num].item;
		destinations[wxid_range_low + num] = entry;
		destinations[wxid_range_low + num].item = tmp;
		destinations[wxid_range_low + num].item->SetItemLabel(towxstring(entry.name + "..."));
	} else {
		//New entry.
		if(destinations.size() == 0)
			PrependSeparator();
		entry.item = Prepend(wxid_range_low + num, towxstring(entry.name + "...")); 
		destinations[wxid_range_low + num] = entry;
	}
	save();
}

std::set<unsigned> upload_menu::entries()
{
	std::set<unsigned> r;
	for(auto i : destinations)
		r.insert(i.first - wxid_range_low);
	return r;
}

upload_menu::upload_entry upload_menu::get_entry(unsigned num)
{
	if(destinations.count(wxid_range_low + num))
		return destinations[wxid_range_low + num];
	else
		throw std::runtime_error("No such upload target");
}

void upload_menu::delete_entry(unsigned num)
{
	if(destinations.count(wxid_range_low + num)) {
		Delete(destinations[wxid_range_low + num].item);
		destinations.erase(wxid_range_low + num);
	}
	save();
}

void upload_menu::on_select(wxCommandEvent& e)
{
	int id = e.GetId();
	modal_pause_holder hld;
	try {
		wxDialog* f;
		if(id == wxid_range_high) {
			f = new wxeditor_uploadtargets(pwin, this);
		} else if(destinations.count(id)) {
			f = new wxeditor_uploaddialog(pwin, destinations[id]);
		} else
			return;
		f->ShowModal();
		f->Destroy();
	} catch(canceled_exception& e) {
		throw;
	} catch(...) {
		throw canceled_exception();
	}
}
