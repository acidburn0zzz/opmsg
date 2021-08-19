/*
 * This file is part of the opmsg crypto message framework.
 *
 * (C) 2015-2021 by Sebastian Krahmer,
 *                  sebastian [dot] krahmer [at] gmail [dot] com
 *
 * opmsg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * opmsg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with opmsg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef opmsg_keystore_h
#define opmsg_keystore_h


#include <map>
#include <vector>
#include <string>
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>

#include "misc.h"
#include "marker.h"


extern "C" {
#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
}

#ifndef HAVE_BN_GENCB_NEW
#define HAVE_BN_GENCB_NEW OPENSSL_VERSION_NUMBER >= 0x10100000L
#endif

namespace opmsg {


class PKEYbox {

public:

	EVP_PKEY *d_pub{nullptr}, *d_priv{nullptr};

	std::string d_pub_pem{""}, d_priv_pem{""}, d_hex{""};

	/* The peer id is assigned if new ephemeral keys are generated to be attached to
	 * a message that is destinated to a certain persona. Only this persona (peer id)
	 * should eventually come back with a kex-id referencing _this_ key. This only affects
	 * Kex keys, not persona keys. It is OK to have an empty peer id.
	 */
	std::string d_peer_id{""};


	PKEYbox(EVP_PKEY *p, EVP_PKEY *s)
		: d_pub(p), d_priv(s)
	{
	}

	virtual ~PKEYbox()
	{
		if (d_pub)
			EVP_PKEY_free(d_pub);
		if (d_priv)
			EVP_PKEY_free(d_priv);
	}

	bool can_sign()
	{
		return d_priv != nullptr;
	}

	bool can_decrypt()
	{
		return d_priv != nullptr;
	}

	bool can_encrypt()
	{
		return d_pub != nullptr;
	}

	bool matches_peer_id(const std::string &s)
	{
		// If no designated peer was recorded, any peer is OK
		if (d_peer_id.size() == 0)
			return 1;
		return d_peer_id == s;
	}

	std::string get_peer_id()
	{
		return d_peer_id;
	}


	void set_peer_id(const std::string &s)
	{
		d_peer_id = s;
	}

	std::string get_pub_pem()
	{
		return d_pub_pem;
	}
};


class DHbox {

public:

	DH *d_pub{nullptr}, *d_priv{nullptr};

	std::string d_pub_pem{""}, d_priv_pem{""}, d_hex{""};

	DHbox(DH *dh1, DH *dh2) : d_pub(dh1), d_priv(dh2)
	{
	}

	virtual ~DHbox()
	{
		if (d_pub)
			DH_free(d_pub);
		if (d_priv)
			DH_free(d_priv);
	}

	bool can_decrypt()
	{
		return d_priv != nullptr;
	}

	bool can_encrypt()
	{
		return d_pub != nullptr;
	}
};


typedef enum : uint32_t {
	LFLAGS_NONE	= 0,
	LFLAGS_NAME	= 1,
	LFLAGS_KEY	= 2,
	LFLAGS_KEX	= 4,
	LFLAGS_ALL	= LFLAGS_NAME|LFLAGS_KEY|LFLAGS_KEX
} load_flags;


class persona {

	std::string d_id{""}, d_name{""}, d_link_src{""}, d_ptype{""}, d_pqsalt{""};

	// The (EC)DH 'session' keys this persona holds
	std::map<std::string, std::vector<PKEYbox *>> d_keys;

	// List of hashes of all imported keys so far
	std::map<std::string, unsigned int> d_imported;

	PKEYbox *d_pkey{nullptr};
	DHbox *d_dh_params{nullptr};

	std::string d_cfgbase{""}, d_err{""};

	template<class T>
	T build_error(const std::string &msg, T r)
	{
		int e = 0;
		d_err = "persona::";
		d_err += msg;
		if ((e = ERR_get_error())) {
			ERR_load_crypto_strings();
			d_err += ":";
			d_err += ERR_error_string(e, nullptr);
			ERR_clear_error();
		} else if (errno) {
			d_err += ":";
			d_err += strerror(errno);
		}
		errno = 0;
		return r;
	}

	int load_dh(const std::string &hex);

public:

	persona(const std::string &dir, const std::string &hash, const std::string &n = "")
		: d_id(hash), d_name(n), d_cfgbase(dir)
	{
		if (!is_hex_hash(d_id))
			d_id = "dead";

		d_ptype = marker::unknown;
	}

	virtual ~persona()
	{
		for (auto i = d_keys.begin(); i != d_keys.end(); ++i) {
			for (auto j = i->second.begin(); j != i->second.end(); ++j)
				delete *j;
		}

		delete d_pkey;
		delete d_dh_params;
	}

	void set_type(const std::string &t)
	{
		if (t == marker::rsa || t == marker::ec)
			d_ptype = t;
	}

	std::string get_type()
	{
		return d_ptype;
	}

	int check_type();

	// is a Post Quantum1 persona?
	bool is_pq1()
	{
		return d_pqsalt.size() > 0;
	}

	bool can_verify()
	{
		return can_encrypt();
	}

	bool can_encrypt()
	{
		return d_pkey != nullptr && d_pkey->d_pub != nullptr;
	}

	bool can_sign()
	{
		return d_pkey != nullptr && d_pkey->d_priv != nullptr;
	}

	bool can_decrypt()
	{
		return can_sign();
	}

	bool can_kex_gen()
	{
		if (d_ptype == marker::rsa)
			return d_dh_params != nullptr && d_dh_params->d_pub != nullptr;
		return true;
	}

	PKEYbox *set_pkey(EVP_PKEY *, EVP_PKEY *);

	PKEYbox *get_pkey()
	{
		return d_pkey;
	}

	std::string get_id()
	{
		return d_id;
	}

	std::string get_pqsalt1()
	{
		return d_pqsalt;
	}

	std::string get_name()
	{
		return d_name;
	}

	std::string linked_src()
	{
		return d_link_src;
	}

	DHbox *new_dh_params();

	DHbox *new_dh_params(const std::string &pem);

	std::vector<PKEYbox *> add_dh_pubkey(const std::string &hash, std::vector<std::string> &pems);

	std::vector<PKEYbox *> add_dh_pubkey(const EVP_MD *md, std::vector<std::string> &pems);

	std::vector<PKEYbox *> gen_kex_key(const std::string &hash, const std::string & = "");

	int gen_dh_key(const EVP_MD *md, std::string&, std::string&, std::string&);

	std::vector<PKEYbox *> gen_kex_key(const EVP_MD *md, const std::string & = "");

	std::vector<PKEYbox *> find_dh_key(const std::string &hex);

	int make_pq1(const std::string &, const std::string &, const std::string &);

	int del_dh_id(const std::string &hex);

	int del_dh_pub(const std::string &hex);

	int del_dh_priv(const std::string &hex);

	bool has_imported(const std::string &hex)
	{
		return d_imported.count(hex) > 0;
	}

	void used_key(const std::string &hex, bool);

	int load(const std::string &hex = "", uint32_t how = LFLAGS_ALL);

	int link(const std::string &hex);

	std::map<std::string, std::vector<PKEYbox *>>::iterator first_key();

	std::map<std::string, std::vector<PKEYbox *>>::iterator end_key();

	std::map<std::string, std::vector<PKEYbox *>>::iterator next_key(const std::map<std::string, std::vector<PKEYbox *>>::iterator &);

	int size()
	{
		return d_keys.size();
	}

	const char *why()
	{
		return d_err.c_str();
	}

	friend class keystore;
};


class keystore {

	std::string d_cfgbase{""};
	std::map<std::string, persona *> d_personas;

	const EVP_MD *d_md{nullptr};

	std::string d_err{""};

	template<class T>
	T build_error(const std::string &msg, T r)
	{
		int e = 0;
		d_err = "keystore::";
		d_err += msg;
		if ((e = ERR_get_error())) {
			ERR_load_crypto_strings();
			d_err += ":";
			d_err += ERR_error_string(e, nullptr);
			ERR_clear_error();
		} else if (errno) {
			d_err += ":";
			d_err += strerror(errno);
		}
		errno = 0;
		return r;
	}

public:

	keystore(const std::string& hash, const std::string &base = ".opmsg")
		: d_cfgbase(base)
	{
		d_md = algo2md(hash);
	}


	~keystore()
	{
		for (auto i : d_personas)
			delete i.second;
	}

	const EVP_MD *md_type()
	{
		return d_md;
	}

	int load(const std::string &id = "", uint32_t how = LFLAGS_ALL);

	int gen_rsa(std::string &pub, std::string &priv);

	int gen_ec(std::string &pub, std::string &priv, int);

	persona *add_persona(const std::string &name, const std::string &rsa_pub_pem, const std::string &rsa_priv_pem, const std::string &dhparams_pem);

	persona *find_persona(const std::string &hex);

	int size()
	{
		return d_personas.size();
	}

	std::map<std::string, persona *>::iterator first_pers();

	std::map<std::string, persona *>::iterator end_pers();

	std::map<std::string, persona *>::iterator next_pers(const std::map<std::string, persona *>::iterator &);


	const char *why()
	{
		return d_err.c_str();
	}
};

int normalize_and_hexhash(const EVP_MD *, std::string &s, std::string &);

} // namespace

#endif






