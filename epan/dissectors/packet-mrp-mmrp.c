/* packet-mrp_mmrp.c
 * Routines for MMRP (MRP Multiple Mac Registration Protocol) dissection
 * Copyright 2011, Johannes Jochen <johannes.jochen[AT]belden.com>
 *
 *
 * Based on the code from packet-mrp-msrp.c (MSRP) from
 * Torrey Atcitty <tatcitty[AT]harman.com> and Craig Gunther <craig.gunther[AT]harman.com>
 * Copyright 2010
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald[AT]wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The MMRP Protocol specification can be found at the following:
 * http://standards.ieee.org/about/get/802/802.1.html
 *
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/etypes.h>

void proto_register_mrp_mmrp(void);
void proto_reg_handoff_mrp_mmrp(void);

/* MMRP End Mark Sequence */
#define MMRP_END_MARK       0x0000

/**********************************************************/
/* Offsets of fields within an MMRP packet                */
/**********************************************************/
#define MMRP_PROTOCOL_VERSION_OFFSET        0

/* Next comes the MMRP Message group */
#define MMRP_MESSAGE_GROUP_OFFSET          (MMRP_PROTOCOL_VERSION_OFFSET + 1) /* Message is a group of fields */
#define MMRP_ATTRIBUTE_TYPE_OFFSET         (MMRP_MESSAGE_GROUP_OFFSET)
#define MMRP_ATTRIBUTE_LENGTH_OFFSET       (MMRP_ATTRIBUTE_TYPE_OFFSET + 1)

/* Next comes the MMRP AttributeList group */
#define MMRP_ATTRIBUTE_LIST_GROUP_OFFSET   (MMRP_ATTRIBUTE_LENGTH_OFFSET + 1) /* AttributeList is a group of fields */

/* Next comes the MMRP VectorAttribute group */
#define MMRP_VECTOR_ATTRIBUTE_GROUP_OFFSET (MMRP_ATTRIBUTE_LIST_GROUP_OFFSET) /* VectorAttribute is a group of fields */
#define MMRP_VECTOR_HEADER_OFFSET          (MMRP_VECTOR_ATTRIBUTE_GROUP_OFFSET) /* contains the following two fields */
#define MMRP_LEAVE_ALL_EVENT_OFFSET        (MMRP_VECTOR_HEADER_OFFSET)
#define MMRP_LEAVE_ALL_EVENT_MASK           0xE000
#define MMRP_NUMBER_OF_VALUES_OFFSET       (MMRP_VECTOR_HEADER_OFFSET)
#define MMRP_NUMBER_OF_VALUES_MASK          0x1fff

/* Next comes the MMRP FirstValue group */
#define MMRP_FIRST_VALUE_GROUP_OFFSET      (MMRP_VECTOR_HEADER_OFFSET + 2) /* FirstValue is a group of fields */

#define MMRP_SERVICE_THREE_PACKED_OFFSET   (MMRP_FIRST_VALUE_GROUP_OFFSET + 1)
#define MMRP_MAC_THREE_PACKED_OFFSET       (MMRP_FIRST_VALUE_GROUP_OFFSET + 6)

/**********************************************************/
/* Valid field contents                                   */
/**********************************************************/

/* Attribute Type definitions */
#define MMRP_ATTRIBUTE_TYPE_SERVICE   0x01
#define MMRP_ATTRIBUTE_TYPE_MAC       0x02
static const value_string attribute_type_vals[] = {
    { MMRP_ATTRIBUTE_TYPE_SERVICE, "Service Requirement" },
    { MMRP_ATTRIBUTE_TYPE_MAC,    "MAC" },
    { 0,                                    NULL }
};

/* Leave All Event definitions */
#define MMRP_NULLLEAVEALL   0
#define MMRP_LEAVEALL       1
static const value_string leave_all_vals[] = {
    { MMRP_NULLLEAVEALL, "Null" },
    { MMRP_LEAVEALL,     "Leave All" },
    { 0,                 NULL }
};

/* Three Packed Event definitions */
static const value_string three_packed_vals[] = {
    { 0, "New" },
    { 1, "JoinIn" },
    { 2, "In" },
    { 3, "JoinMt" },
    { 4, "Mt" },
    { 5, "Lv" },
    { 0, NULL }
};

/**********************************************************/
/* Initialize the protocol and registered fields          */
/**********************************************************/
static int proto_mmrp = -1;
static int hf_mmrp_proto_id = -1;
static int hf_mmrp_message = -1; /* Message is a group of fields */
static int hf_mmrp_attribute_type = -1;
static int hf_mmrp_attribute_length = -1;
static int hf_mmrp_attribute_list = -1; /* AttributeList is a group of fields */
static int hf_mmrp_vector_attribute = -1; /* VectorAttribute is a group of fields */

/* The following VectorHeader contains the LeaveAllEvent and NumberOfValues */
static int hf_mmrp_vector_header = -1;
static int hf_mmrp_leave_all_event = -1;
static int hf_mmrp_number_of_values = -1;
static gint ett_vector_header = -1;
static int * const vector_header_fields[] = {
    &hf_mmrp_leave_all_event,
    &hf_mmrp_number_of_values,
    NULL
};

static int hf_mmrp_first_value = -1; /* FirstValue is a group of fields */

static int hf_mmrp_mac = -1;
static int hf_mmrp_ser_req = -1;

static int hf_mmrp_three_packed_event = -1;

static int hf_mmrp_end_mark = -1;

/* Initialize the subtree pointers */
static gint ett_mmrp = -1;
static gint ett_msg = -1;
static gint ett_attr_list = -1;
static gint ett_vect_attr = -1;
static gint ett_first_value = -1;



/**********************************************************/
/* Dissector starts here                                  */
/**********************************************************/

/* dissect_mmrp_common1 (called from dissect_mmrp)
 *
 * dissect the following fields which are common to all MMRP attributes:
 *   Attribute Type
 *   Attribute Length
 *   Attribute List Length
 */
static void
dissect_mmrp_common1(proto_tree *msg_tree, tvbuff_t *tvb, int msg_offset)
{
    proto_tree_add_item(msg_tree, hf_mmrp_attribute_type, tvb,
                        MMRP_ATTRIBUTE_TYPE_OFFSET + msg_offset, 1, ENC_BIG_ENDIAN);
    proto_tree_add_item(msg_tree, hf_mmrp_attribute_length, tvb,
                        MMRP_ATTRIBUTE_LENGTH_OFFSET + msg_offset, 1, ENC_BIG_ENDIAN);
}


/* dissect_mmrp_common2 (called from dissect_mmrp)
 *
 * dissect the following fields which are common to all MMRP attributes:
 *   Leave All Event
 *   Number of Values fields
 */
static void
dissect_mmrp_common2(proto_tree *vect_attr_tree, tvbuff_t *tvb, int msg_offset)
{
    proto_tree_add_bitmask(vect_attr_tree, tvb, MMRP_VECTOR_HEADER_OFFSET + msg_offset,
                           hf_mmrp_vector_header, ett_vector_header, vector_header_fields, ENC_BIG_ENDIAN);
}

/* dissect_mmrp_three_packed_event (called from dissect_mmrp)
 *
 * dissect one or more ThreePackedEvents
 */
static guint
dissect_mmrp_three_packed_event(proto_tree *vect_attr_tree, tvbuff_t *tvb, guint offset, guint16 number_of_values)
{
    guint counter;

    for ( counter = 0; counter < number_of_values; ) {
        guint8 value;
        guint8 three_packed_event[3];

        value = tvb_get_guint8(tvb, offset);
        three_packed_event[0] = value / 36;
        value -= 36 * three_packed_event[0];
        three_packed_event[1] = value / 6;
        value -=  6 * three_packed_event[1];
        three_packed_event[2] = value;

        proto_tree_add_uint(vect_attr_tree, hf_mmrp_three_packed_event, tvb, offset, sizeof(guint8),
                            three_packed_event[0]);
        counter++;
        if ( counter < number_of_values ) {
            proto_tree_add_uint(vect_attr_tree, hf_mmrp_three_packed_event, tvb, offset, sizeof(guint8),
                                three_packed_event[1]);
            counter++;
        }
        if ( counter < number_of_values ) {
            proto_tree_add_uint(vect_attr_tree, hf_mmrp_three_packed_event, tvb, offset, sizeof(guint8),
                                three_packed_event[2]);
            counter++;
        }

        offset++;
    }
    return( offset );
}

/* dissect_main
 *
 * main dissect function that calls the other functions listed above as necessary
 */
static int
dissect_mmrp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data _U_) {
    /* Set up structures needed to add the protocol subtrees and manage them */
    proto_item *ti, *msg_ti, *attr_list_ti, *vect_attr_ti, *first_value_ti;
    proto_tree *mmrp_tree, *msg_tree, *attr_list_tree, *vect_attr_tree, *first_value_tree;

    /* Make entries in Protocol column and Info column on summary display */
    col_set_str(pinfo->cinfo, COL_PROTOCOL, "MRP-MMRP");

    col_set_str(pinfo->cinfo, COL_INFO, "Multiple Mac Registration Protocol");

    if (tree) {
        guint8 attribute_type;
        guint8 attribute_length;
        guint16 number_of_values;
        guint offset = 0;
        int vect_attr_len;
        int msg_offset;  /* Use when handling multiple messages.  This points to current msg being decoded. */
        int vect_offset; /* Use when handling multiple vector attributes.  This points to the current vector attribute being decoded. */

        ti = proto_tree_add_item(tree, proto_mmrp, tvb, 0, -1, ENC_NA);
        mmrp_tree = proto_item_add_subtree(ti, ett_mmrp);

        proto_tree_add_item(mmrp_tree, hf_mmrp_proto_id, tvb, MMRP_PROTOCOL_VERSION_OFFSET, 1, ENC_BIG_ENDIAN);

        /* MMRP supports multiple MRP Messages per frame.  Handle those Messages in
         * the following while() loop. You will know you are at the end of the list
         * of messages when the EndMark (0x0000) is encountered instead of an
         * Attribute Type and Attribute Length (guaranteed to not be 0x0000).
         */
        msg_offset = 0;
        while (tvb_get_ntohs(tvb, MMRP_ATTRIBUTE_TYPE_OFFSET + msg_offset) != MMRP_END_MARK) {

            attribute_type = tvb_get_guint8(tvb, MMRP_ATTRIBUTE_TYPE_OFFSET + msg_offset);
            attribute_length = tvb_get_guint8(tvb, MMRP_ATTRIBUTE_LENGTH_OFFSET + msg_offset);

            /* MMRP Message is a group of fields
             *
             * Contains AttributeType (1 byte)
             *        + AttributeLength (1 byte)
             *        + AttributeList (AttributeListLength bytes)
            *        bytes of data
            */
            msg_ti = proto_tree_add_item(mmrp_tree, hf_mmrp_message, tvb,
                                         MMRP_MESSAGE_GROUP_OFFSET + msg_offset,
                                         -1, ENC_NA);
            msg_tree = proto_item_add_subtree(msg_ti, ett_msg);

            /* Append AttributeType description to the end of the "Message" heading */
            proto_item_append_text(msg_tree, ": %s (%d)",
                                   val_to_str_const(attribute_type, attribute_type_vals, "<Unknown>"),
                                   attribute_type);

            dissect_mmrp_common1(msg_tree, tvb, msg_offset);

            /* MMRP AttributeList is a group of fields
             *
             * Contains AttributeListLength bytes of data NOT
             */
            attr_list_ti = proto_tree_add_item(msg_tree, hf_mmrp_attribute_list, tvb,
                                               MMRP_ATTRIBUTE_LIST_GROUP_OFFSET + msg_offset,
                                               -1, ENC_NA);
            attr_list_tree = proto_item_add_subtree(attr_list_ti, ett_attr_list);


            /* MMRP supports multiple MRP Vector Attributes per Attribute List.  Handle those
             * Vector Attributes in the following while() loop. You will know you are at the
             * end of the list of Vector Attributes when the EndMark (0x0000) is encountered
             * instead of a Vector Header (guaranteed to not be 0x0000).
             */
            vect_offset = 0;
            while (tvb_get_ntohs(tvb, MMRP_VECTOR_HEADER_OFFSET + msg_offset + vect_offset) != MMRP_END_MARK) {
                /* MMRP VectorAttribute is a group of fields
                 *
                 * Contains VectorHeader (2 bytes)
                 *        + FirstValue (AttributeLength bytes)
                 *        + VectorThreePacked (NumberOfValues @ 3/vector bytes)
                 *        + VectorFourPacked (NumberOfValues @ 4/vector bytes only for Listener attributes)
                 *        bytes of data
                 */
                number_of_values = tvb_get_ntohs(tvb, MMRP_NUMBER_OF_VALUES_OFFSET + msg_offset + vect_offset)
                                   & MMRP_NUMBER_OF_VALUES_MASK;

                vect_attr_len = 2 + attribute_length + (number_of_values + 2)/3; /* stores 3 values per byte */

                vect_attr_ti = proto_tree_add_item(attr_list_tree, hf_mmrp_vector_attribute, tvb,
                                                   MMRP_VECTOR_ATTRIBUTE_GROUP_OFFSET + msg_offset + vect_offset,
                                                   vect_attr_len, ENC_NA);

                vect_attr_tree = proto_item_add_subtree(vect_attr_ti, ett_vect_attr);

                dissect_mmrp_common2(vect_attr_tree, tvb, msg_offset + vect_offset);

                if (attribute_type == MMRP_ATTRIBUTE_TYPE_MAC) {
                    /* MMRP FirstValue is a Mac Address*/
                    first_value_ti = proto_tree_add_item(vect_attr_tree, hf_mmrp_first_value, tvb,
                                        MMRP_FIRST_VALUE_GROUP_OFFSET + msg_offset + vect_offset,
                                        attribute_length, ENC_NA);
                    first_value_tree = proto_item_add_subtree(first_value_ti, ett_first_value);

                    /* Add MAC components to First Value tree */
                    proto_tree_add_item(first_value_tree, hf_mmrp_mac, tvb,
                                        MMRP_FIRST_VALUE_GROUP_OFFSET + msg_offset + vect_offset, 6, ENC_NA);

                    /* Decode three packed events. */
                    offset = dissect_mmrp_three_packed_event(vect_attr_tree, tvb,
                                                             MMRP_MAC_THREE_PACKED_OFFSET + msg_offset + vect_offset,
                                                             number_of_values);
                }
                else if (attribute_type == MMRP_ATTRIBUTE_TYPE_SERVICE) {
                    /* MMRP Service Requirement*/
                    first_value_ti = proto_tree_add_item(vect_attr_tree, hf_mmrp_first_value, tvb,
                                        MMRP_FIRST_VALUE_GROUP_OFFSET + msg_offset + vect_offset,
                                        attribute_length, ENC_NA);
                    first_value_tree = proto_item_add_subtree(first_value_ti, ett_first_value);

                    /* Add ServiceRequirement components to First Value tree */
                    proto_tree_add_item(first_value_tree, hf_mmrp_ser_req, tvb,
                                        MMRP_FIRST_VALUE_GROUP_OFFSET + msg_offset + vect_offset, 1, ENC_BIG_ENDIAN);

                    offset = dissect_mmrp_three_packed_event(vect_attr_tree, tvb,
                                                             MMRP_SERVICE_THREE_PACKED_OFFSET + msg_offset + vect_offset,
                                                             number_of_values);
                }

                vect_offset += vect_attr_len; /* Move to next Vector Attribute, if there is one */
            } /* Multiple VectorAttribute while() */

            proto_tree_add_item(attr_list_tree, hf_mmrp_end_mark, tvb, offset, 2, ENC_BIG_ENDIAN); /* VectorAttribute EndMark */

            msg_offset += vect_offset + 2/* + VectorHeader */ + 2/* + endmark */;

        } /* Multiple Message while() */

        proto_tree_add_item(mmrp_tree, hf_mmrp_end_mark, tvb, offset+2, 2, ENC_BIG_ENDIAN); /* Message EndMark */
    }
    return tvb_captured_length(tvb);
}


/* Register the protocol with Wireshark */
void
proto_register_mrp_mmrp(void)
{
    static hf_register_info hf[] = {
        { &hf_mmrp_proto_id,
            { "Protocol Version",      "mrp-mmrp.protocol_version",
              FT_UINT8,  BASE_DEC, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_message, /* Message is a group of fields */
            { "Message",               "mrp-mmrp.message",
              FT_NONE,  BASE_NONE, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_attribute_type,
            { "Attribute Type",        "mrp-mmrp.attribute_type",
              FT_UINT8,  BASE_DEC, VALS(attribute_type_vals), 0x0, NULL, HFILL }
        },
        { &hf_mmrp_attribute_length,
            { "Attribute Length",      "mrp-mmrp.attribute_length",
              FT_UINT8,  BASE_DEC, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_attribute_list, /* AttributeList is a group of fields */
            { "Attribute List",        "mrp-mmrp.attribute_list",
              FT_NONE,  BASE_NONE, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_vector_attribute, /* VectorAttribute is a group of fields */
            { "Vector Attribute",      "mrp-mmrp.vector_attribute",
              FT_NONE,  BASE_NONE, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_vector_header,
            { "Vector Header",         "mrp-mmrp.vector_header",
              FT_UINT16, BASE_HEX, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_leave_all_event,
            { "Leave All Event",       "mrp-mmrp.leave_all_event",
              FT_UINT16, BASE_DEC, VALS(leave_all_vals), MMRP_LEAVE_ALL_EVENT_MASK, NULL, HFILL }
        },
        { &hf_mmrp_number_of_values,
            { "Number of Values",      "mrp-mmrp.number_of_values",
              FT_UINT16, BASE_DEC, NULL, MMRP_NUMBER_OF_VALUES_MASK, NULL, HFILL }
        },
        { &hf_mmrp_first_value, /* FirstValue is a group of fields */
            { "First Value",           "mrp-mmrp.first_value",
              FT_NONE,  BASE_NONE, NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_mac,
            { "MAC",                   "mrp-mmrp.mac",
              FT_ETHER,  BASE_NONE, NULL, 0x00, NULL, HFILL }
        },
        { &hf_mmrp_ser_req,
            { "Service Requirement",   "mrp-mmrp.service_requirement",
              FT_UINT8, BASE_DEC,  NULL, 0x0, NULL, HFILL }
        },
        { &hf_mmrp_three_packed_event,
            { "Attribute Event",       "mrp-mmrp.three_packed_event",
              FT_UINT8, BASE_DEC,  VALS(three_packed_vals), 0x0, NULL, HFILL }
        },
        { &hf_mmrp_end_mark,
            { "End Mark",              "mrp-mmrp.end_mark",
              FT_UINT16, BASE_HEX, NULL, 0x0, NULL, HFILL }
        },
    };

    /* Setup protocol subtree array */
    static gint *ett[] = {
        &ett_mmrp,
        &ett_msg,
        &ett_attr_list,
        &ett_vect_attr,
        &ett_vector_header,
        &ett_first_value
    };

    /* Register the protocol name and description */
    proto_mmrp = proto_register_protocol("Multiple Mac Registration Protocol",
                                         "MRP-MMRP", "mrp-mmrp");

    /* Required function calls to register the header fields and subtrees used */
    proto_register_field_array(proto_mmrp, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_mrp_mmrp(void)
{
    dissector_handle_t mmrp_handle;

    mmrp_handle = create_dissector_handle(dissect_mmrp, proto_mmrp);
    dissector_add_uint("ethertype", ETHERTYPE_MMRP, mmrp_handle);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
