#include "platform/wxwidgets/settings-common.hpp"
#include "core/settings.hpp"

namespace
{
	class wxeditor_esettings_advanced : public settings_tab
	{
	public:
		wxeditor_esettings_advanced(wxWindow* parent);
		~wxeditor_esettings_advanced();
		void on_change(wxCommandEvent& e);
		void on_selchange(wxCommandEvent& e);
		void on_setting_change(const setting_var_base& val);
		void _refresh();
		struct listener : public setting_var_listener
		{
			listener(setting_var_group& group, wxeditor_esettings_advanced& _obj)
				: grp(group), obj(_obj)
			{
				group.add_listener(*this);
			}
			~listener() throw()
			{
				grp.remove_listener(*this);
			}
			void on_setting_change(setting_var_group& grp, const setting_var_base& val)
			{
				obj.on_setting_change(val);
			}
			wxeditor_esettings_advanced& obj;
			setting_var_group& grp;
		};
	private:
		listener _listener;
		void refresh();
		std::set<std::string> settings;
		std::map<std::string, std::string> values;
		std::map<std::string, std::string> names;
		std::map<int, std::string> selections;
		std::string selected();
		wxButton* changebutton;
		wxListBox* _settings;
	};

	wxeditor_esettings_advanced::wxeditor_esettings_advanced(wxWindow* parent)
		: settings_tab(parent), _listener(lsnes_vset, *this)
	{
		wxSizer* top_s = new wxBoxSizer(wxVERTICAL);
		SetSizer(top_s);

		top_s->Add(_settings = new wxListBox(this, wxID_ANY), 1, wxGROW);
		_settings->Connect(wxEVT_COMMAND_LISTBOX_SELECTED,
			wxCommandEventHandler(wxeditor_esettings_advanced::on_selchange), NULL, this);

		wxBoxSizer* pbutton_s = new wxBoxSizer(wxHORIZONTAL);
		pbutton_s->AddStretchSpacer();
		pbutton_s->Add(changebutton = new wxButton(this, wxID_ANY, wxT("Change")), 0, wxGROW);
		changebutton->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
			wxCommandEventHandler(wxeditor_esettings_advanced::on_change), NULL, this);
		top_s->Add(pbutton_s, 0, wxGROW);

		refresh();
		wxCommandEvent e;
		on_selchange(e);
		top_s->SetSizeHints(this);
		Fit();
	}

	wxeditor_esettings_advanced::~wxeditor_esettings_advanced()
	{
	}

	std::string change_value_of_boolean(const std::string& name, const setting_var_description& desc,
		const std::string& current)
	{
		return string_to_bool(current) ? "0" : "1";
	}

	std::string change_value_of_enumeration(wxWindow* parent, const std::string& name,
		const setting_var_description& desc, const std::string& current)
	{
		std::vector<std::string> valset;
		unsigned dflt = 0;
		for(unsigned i = 0; i <= desc.enumeration->max_val(); i++) {
			valset.push_back(desc.enumeration->get(i));
			if(desc.enumeration->get(i) == current)
				dflt = i;
		}
		return pick_among(parent, "Set value to", "Set " + name + " to value:", valset, dflt);
	}

	std::string change_value_of_string(wxWindow* parent, const std::string& name,
		const setting_var_description& desc, const std::string& current)
	{
		return pick_text(parent, "Set value to", "Set " + name + " to value:", current);
	}

	class numeric_inputbox : public wxDialog
	{
	public:
		numeric_inputbox(wxWindow* parent, const std::string& name, int64_t minval, int64_t maxval,
			const std::string& val)
			: wxDialog(parent, wxID_ANY, wxT("Set value to"))
		{
			wxSizer* s1 = new wxBoxSizer(wxVERTICAL);
			SetSizer(s1);
			s1->Add(new wxStaticText(this, wxID_ANY, towxstring("Set " + name + " to value:")), 0,
				wxGROW);

			s1->Add(sp = new wxSpinCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize,
				wxSP_ARROW_KEYS, minval, maxval, parse_value<int64_t>(val)), 1, wxGROW);

			wxBoxSizer* pbutton_s = new wxBoxSizer(wxHORIZONTAL);
			pbutton_s->AddStretchSpacer();
			wxButton* t;
			pbutton_s->Add(t = new wxButton(this, wxID_CANCEL, wxT("Cancel")), 0, wxGROW);
			t->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
				wxCommandEventHandler(numeric_inputbox::on_button), NULL, this);
			pbutton_s->Add(t = new wxButton(this, wxID_OK, wxT("OK")), 0, wxGROW);
			t->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
				wxCommandEventHandler(numeric_inputbox::on_button), NULL, this);
			s1->Add(pbutton_s, 0, wxGROW);

			s1->SetSizeHints(this);
		}
		std::string get_value() { return (stringfmt() << sp->GetValue()).str(); }
		void on_button(wxCommandEvent& e) { EndModal(e.GetId()); }
	private:
		wxSpinCtrl* sp;
	};

	class path_inputbox : public wxDialog
	{
	public:
		path_inputbox(wxWindow* parent, const std::string& name, const std::string& val)
			: wxDialog(parent, wxID_ANY, wxT("Set path to"))
		{
			wxButton* t;
			wxSizer* s1 = new wxBoxSizer(wxVERTICAL);
			SetSizer(s1);
			s1->Add(new wxStaticText(this, wxID_ANY, towxstring("Set " + name + " to value:")), 0,
				wxGROW);
			wxSizer* s2 = new wxBoxSizer(wxHORIZONTAL);
			s2->Add(pth = new wxTextCtrl(this, wxID_ANY, towxstring(val), wxDefaultPosition,
				wxSize(400, -1)), 1, wxGROW);
			s2->Add(t = new wxButton(this, wxID_HIGHEST + 1, wxT("...")), 0, wxGROW);
			t->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
				wxCommandEventHandler(path_inputbox::on_pbutton), NULL, this);
			s1->Add(s2, 1, wxGROW);

			wxBoxSizer* pbutton_s = new wxBoxSizer(wxHORIZONTAL);
			pbutton_s->AddStretchSpacer();
			pbutton_s->Add(t = new wxButton(this, wxID_CANCEL, wxT("Cancel")), 0, wxGROW);
			t->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
				wxCommandEventHandler(path_inputbox::on_button), NULL, this);
			pbutton_s->Add(t = new wxButton(this, wxID_OK, wxT("OK")), 0, wxGROW);
			t->Connect(wxEVT_COMMAND_BUTTON_CLICKED,
				wxCommandEventHandler(path_inputbox::on_button), NULL, this);
			s1->Add(pbutton_s, 0, wxGROW);

			s1->SetSizeHints(this);
		}
		std::string get_value() { return tostdstring(pth->GetValue()); }
		void on_pbutton(wxCommandEvent& e) {
			wxDirDialog* d;
			d = new wxDirDialog(this, wxT("Select project directory"),
				pth->GetValue(), wxDD_DIR_MUST_EXIST);
			if(d->ShowModal() == wxID_CANCEL) {
				d->Destroy();
				return;
			}
			pth->SetValue(d->GetPath());
			d->Destroy();
		}
		void on_button(wxCommandEvent& e) {
			wxDirDialog* d;
			switch(e.GetId()) {
			case wxID_OK:
			case wxID_CANCEL:
				EndModal(e.GetId());
				break;
			};
		}
	private:
		wxTextCtrl* pth;
	};

	std::string change_value_of_numeric(wxWindow* parent, const std::string& name,
		const setting_var_description& desc, const std::string& current)
	{
		auto d = new numeric_inputbox(parent, name, desc.min_val, desc.max_val, current);
		int x = d->ShowModal();
		if(x == wxID_CANCEL) {
			d->Destroy();
			throw canceled_exception();
		}
		std::string v = d->get_value();
		d->Destroy();
		return v;
	}

	std::string change_value_of_path(wxWindow* parent, const std::string& name,
		const setting_var_description& desc, const std::string& current)
	{
		auto d = new path_inputbox(parent, name, current);
		int x = d->ShowModal();
		if(x == wxID_CANCEL) {
			d->Destroy();
			throw canceled_exception();
		}
		std::string v = d->get_value();
		d->Destroy();
		return v;
	}

	void wxeditor_esettings_advanced::on_change(wxCommandEvent& e)
	{
		if(closing())
			return;
		std::string name = selected();
		if(name == "")
			return;
		std::string value;
		std::string err;
		value = lsnes_vsetc.get(name);
		auto model = lsnes_vsetc.get_description(name);
		try {
			switch(model.type) {
			case setting_var_description::T_BOOLEAN:
				value = change_value_of_boolean(name, model, value); break;
			case setting_var_description::T_NUMERIC:
				value = change_value_of_numeric(this, name, model, value); break;
			case setting_var_description::T_STRING:
				value = change_value_of_string(this, name, model, value); break;
			case setting_var_description::T_PATH:
				value = change_value_of_path(this, name, model, value); break;
			case setting_var_description::T_ENUMERATION:
				value = change_value_of_enumeration(this, name, model, value); break;
			default:
				value = change_value_of_string(this, name, model, value); break;
			};
		} catch(...) {
			return;
		}
		bool error = false;
		std::string errorstr;
		runemufn([&error, &errorstr, name, value]() {
			try {
				lsnes_vsetc.set(name, value);
			} catch(std::exception& e) {
				error = true;
				errorstr = e.what();
			}
		});
		if(error)
			wxMessageBox(towxstring(errorstr), wxT("Error setting value"), wxICON_EXCLAMATION | wxOK);
	}

	void wxeditor_esettings_advanced::on_selchange(wxCommandEvent& e)
	{
		if(closing())
			return;
		std::string sel = selected();
		bool enable = (sel != "");
		changebutton->Enable(enable);
	}

	void wxeditor_esettings_advanced::on_setting_change(const setting_var_base& val)
	{
		if(closing())
			return;
		runuifun([this, &val]() {
			std::string setting = val.get_iname();
			std::string value = val.str();
			this->settings.insert(setting);
			this->values[setting] = value;
			this->_refresh();
		});
	}

	void wxeditor_esettings_advanced::refresh()
	{
		if(closing())
			return;
		settings = lsnes_vsetc.get_keys();
		for(auto i : settings) {
			values[i] = lsnes_vsetc.get(i);
			names[i] = lsnes_vset[i].get_hname();
		}
		_refresh();
	}

	std::string wxeditor_esettings_advanced::selected()
	{
		if(closing())
			return "";
		int x = _settings->GetSelection();
		if(selections.count(x))
			return selections[x];
		else
			return "";
	}

	void wxeditor_esettings_advanced::_refresh()
	{
		if(closing())
			return;
		std::vector<wxString> strings;
		std::multimap<std::string, std::string> sort;
		int k = 0;
		for(auto i : settings)
			sort.insert(std::make_pair(names[i], i));
		for(auto i : sort) {
			auto description = lsnes_vsetc.get_description(i.second);
			strings.push_back(towxstring(names[i.second] + " (Value: " + values[i.second] + ")"));
			selections[k++] = i.second;
		}
		_settings->Set(strings.size(), &strings[0]);
	}

	settings_tab_factory advanced_tab("Advanced", [](wxWindow* parent) -> settings_tab* {
		return new wxeditor_esettings_advanced(parent);
	});
}