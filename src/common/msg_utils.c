/* (C) 2014 by sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <osmo-bts/msg_utils.h>
#include <osmo-bts/logging.h>
#include <osmo-bts/oml.h>

#include <osmocom/gsm/protocol/ipaccess.h>
#include <osmocom/gsm/protocol/gsm_12_21.h>
#include <osmocom/gsm/abis_nm.h>
#include <osmocom/core/msgb.h>

#include <arpa/inet.h>


static int check_fom(struct abis_om_hdr *omh, size_t len)
{
	if (omh->length != len) {
		LOGP(DL1C, LOGL_ERROR, "Incorrect OM hdr length value %d %zu\n",
		     omh->length, len);
		return -1;
	}

	if (len < sizeof(struct abis_om_fom_hdr)) {
		LOGP(DL1C, LOGL_ERROR, "FOM header insufficient space %zu %zu\n",
		     len, sizeof(struct abis_om_fom_hdr));
		return -1;
	}

	return 0;
}

static int check_manuf(struct msgb *msg, struct abis_om_hdr *omh, size_t msg_size)
{
	int type;
	size_t size;

	if (msg_size < 1) {
		LOGP(DL1C, LOGL_ERROR, "No ManId Length Indicator %zu\n",
		     msg_size);
		return -1;
	}

	if (omh->data[0] >= msg_size - 1) {
		LOGP(DL1C, LOGL_ERROR,
		     "Insufficient message space for this ManId Length %d %zu\n",
		     omh->data[0], msg_size - 1);
		return -1;
	}

	if (omh->data[0] == sizeof(abis_nm_ipa_magic) &&
		strncmp(abis_nm_ipa_magic, (const char *)omh->data + 1,
			sizeof(abis_nm_ipa_magic)) == 0) {
		type = OML_MSG_TYPE_IPA;
		size = sizeof(abis_nm_ipa_magic) + 1;
	} else if (omh->data[0] == sizeof(abis_nm_osmo_magic) &&
		strncmp(abis_nm_osmo_magic, (const char *) omh->data + 1,
			sizeof(abis_nm_osmo_magic)) == 0) {
		type = OML_MSG_TYPE_OSMO;
		size = sizeof(abis_nm_osmo_magic) + 1;
	} else {
		LOGP(DL1C, LOGL_ERROR, "Manuf Label Unknown\n");
		return -1;
	}

	/* we have verified that the vendor string fits */
	msg->l3h = omh->data + size;
	if (check_fom(omh, msgb_l3len(msg)) != 0)
		return -1;
	return type;
}

/**
 * Return 0 in case the IPA structure is okay and in this
 * case the l2h will be set to the beginning of the data.
 */
int msg_verify_ipa_structure(struct msgb *msg)
{
	struct ipaccess_head *hh;

	if (msgb_l1len(msg) < sizeof(struct ipaccess_head)) {
		LOGP(DL1C, LOGL_ERROR,
			"Ipa header insufficient space %d %d\n",
			msgb_l1len(msg), sizeof(struct ipaccess_head));
		return -1;
	}

	hh = (struct ipaccess_head *) msg->l1h;

	if (hh->proto != IPAC_PROTO_OML) {
		LOGP(DL1C, LOGL_ERROR,
			"Incorrect ipa header protocol 0x%x 0x%x\n",
			hh->proto, IPAC_PROTO_OML);
		return -1;
	}

	if (ntohs(hh->len) != msgb_l1len(msg) - sizeof(struct ipaccess_head)) {
		LOGP(DL1C, LOGL_ERROR,
			"Incorrect ipa header msg size %d %d\n",
			ntohs(hh->len), msgb_l1len(msg) - sizeof(struct ipaccess_head));
		return -1;
	}

	msg->l2h = hh->data;
	return 0;
}

/**
 * \brief Verify the structure of the OML message and set l3h
 *
 * This function verifies that the data in \param in msg is a proper
 * OML message. This code assumes that msg->l2h points to the
 * beginning of the OML message. In the successful case the msg->l3h
 * will be set and will point to the FOM header. The value is undefined
 * in all other cases.
 *
 * \param msg The message to analyze starting from msg->l2h.
 * \return In case the structure is correct a positive number will be
 * returned and msg->l3h will point to the FOM. The number is a
 * classification of the vendor type of the message.
 */
int msg_verify_oml_structure(struct msgb *msg)
{
	struct abis_om_hdr *omh;

	if (msgb_l2len(msg) < sizeof(*omh)) {
		LOGP(DL1C, LOGL_ERROR, "Om header insufficient space %d %d\n",
		     msgb_l2len(msg), sizeof(*omh));
		return -1;
	}

	omh = (struct abis_om_hdr *) msg->l2h;
	if (omh->mdisc != ABIS_OM_MDISC_FOM &&
	    omh->mdisc != ABIS_OM_MDISC_MANUF) {
		LOGP(DL1C, LOGL_ERROR, "Incorrect om mdisc value %x\n",
		     omh->mdisc);
		return -1;
	}

	if (omh->placement != ABIS_OM_PLACEMENT_ONLY) {
		LOGP(DL1C, LOGL_ERROR, "Incorrect om placement value %x %x\n",
		     omh->placement, ABIS_OM_PLACEMENT_ONLY);
		return -1;
	}

	if (omh->sequence != 0) {
		LOGP(DL1C, LOGL_ERROR, "Incorrect om sequence value %d\n",
		     omh->sequence);
		return -1;
	}

	if (omh->mdisc == ABIS_OM_MDISC_MANUF)
		return check_manuf(msg, omh, msgb_l2len(msg) - sizeof(*omh));

	msg->l3h = omh->data;
	if (check_fom(omh, msgb_l3len(msg)) != 0)
		return -1;
	return OML_MSG_TYPE_ETSI;
}
