/*
 * Generated by asn1c-0.9.28 (http://lionet.info/asn1c)
 * From ASN.1 module "Crypto-Conditions"
 * 	found in "CryptoConditions.asn"
 */

#include "SimpleSha256Condition.h"

static int
cost_3_constraint(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}
	
	
	/* Constraint check succeeded */
	return 0;
}

/*
 * This type is implemented using NativeInteger,
 * so here we adjust the DEF accordingly.
 */
static void
cost_3_inherit_TYPE_descriptor(asn_TYPE_descriptor_t *td) {
	td->op->free_struct    = asn_DEF_NativeInteger.op->free_struct;
	td->op->print_struct   = asn_DEF_NativeInteger.op->print_struct;
	td->encoding_constraints = asn_DEF_NativeInteger.encoding_constraints;
	td->op->ber_decoder    = asn_DEF_NativeInteger.op->ber_decoder;
	td->op->der_encoder    = asn_DEF_NativeInteger.op->der_encoder;
	td->op->xer_decoder    = asn_DEF_NativeInteger.op->xer_decoder;
	td->op->xer_encoder    = asn_DEF_NativeInteger.op->xer_encoder;
	td->op->uper_decoder   = asn_DEF_NativeInteger.op->uper_decoder;
	td->op->uper_encoder   = asn_DEF_NativeInteger.op->uper_encoder;
	if(!td->encoding_constraints.per_constraints)
		td->encoding_constraints.per_constraints = asn_DEF_NativeInteger.encoding_constraints.per_constraints;
	td->elements       = asn_DEF_NativeInteger.elements;
	td->elements_count = asn_DEF_NativeInteger.elements_count;
     /* td->specifics      = asn_DEF_NativeInteger.specifics;	// Defined explicitly */
}

static void
cost_3_free(const asn_TYPE_descriptor_t *td,
		void *struct_ptr, enum asn_struct_free_method  contents_only) {
	cost_3_inherit_TYPE_descriptor(td);
	td->op->free_struct(td, struct_ptr, contents_only);
}

static int
cost_3_print(const asn_TYPE_descriptor_t *td, const void *struct_ptr,
		int ilevel, asn_app_consume_bytes_f *cb, void *app_key) {
	cost_3_inherit_TYPE_descriptor(td);
	return td->op->print_struct(td, struct_ptr, ilevel, cb, app_key);
}

static asn_dec_rval_t
cost_3_decode_ber(const asn_codec_ctx_t *opt_codec_ctx, const asn_TYPE_descriptor_t *td,
		void **structure, const void *bufptr, size_t size, int tag_mode) {
	cost_3_inherit_TYPE_descriptor(td);
	return td->op->ber_decoder(opt_codec_ctx, td, structure, bufptr, size, tag_mode);
}

static asn_enc_rval_t
cost_3_encode_der(const asn_TYPE_descriptor_t *td,
		const void *structure, int tag_mode, ber_tlv_tag_t tag,
		asn_app_consume_bytes_f *cb, void *app_key) {
	cost_3_inherit_TYPE_descriptor(td);
	return td->op->der_encoder(td, structure, tag_mode, tag, cb, app_key);
}

static asn_dec_rval_t
cost_3_decode_xer(const asn_codec_ctx_t *opt_codec_ctx, const asn_TYPE_descriptor_t *td,
		void **structure, const char *opt_mname, const void *bufptr, size_t size) {
	cost_3_inherit_TYPE_descriptor(td);
	return td->op->xer_decoder(opt_codec_ctx, td, structure, opt_mname, bufptr, size);
}

static asn_enc_rval_t
cost_3_encode_xer(const asn_TYPE_descriptor_t *td, const void *structure,
		int ilevel, enum xer_encoder_flags_e flags,
		asn_app_consume_bytes_f *cb, void *app_key) {
	cost_3_inherit_TYPE_descriptor(td);
	return td->op->xer_encoder(td, structure, ilevel, flags, cb, app_key);
}

static int
memb_fingerprint_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	const OCTET_STRING_t *st = (const OCTET_STRING_t *)sptr;
	size_t size;
	
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}
	
	size = st->size;
	
	if((size == 32)) {
		/* Constraint check succeeded */
		return 0;
	} else {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: constraint failed (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}
}

static int
memb_cost_constraint_1(const asn_TYPE_descriptor_t *td, const void *sptr,
			asn_app_constraint_failed_f *ctfailcb, void *app_key) {
	
	if(!sptr) {
		ASN__CTFAIL(app_key, td, sptr,
			"%s: value not given (%s:%d)",
			td->name, __FILE__, __LINE__);
		return -1;
	}
	
	
	/* Constraint check succeeded */
	return 0;
}

static asn_INTEGER_specifics_t asn_SPC_cost_specs_3 = {
	0,	0,	0,	0,	0,
	0,	/* Native long size */
	1	/* Unsigned representation */
};
static const ber_tlv_tag_t asn_DEF_cost_tags_3[] = {
	(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
	(ASN_TAG_CLASS_UNIVERSAL | (2 << 2))
};

static const asn_TYPE_operation_t asn_CHOICE_cost_3 = {
	cost_3_free,
	cost_3_print,
	0,
	cost_3_decode_ber,
	cost_3_encode_der,
	cost_3_decode_xer,
	cost_3_encode_xer,
	0,
	0,
	0,
	0,
	0,
	0
};

static /* Use -fall-defs-global to expose */
asn_TYPE_descriptor_t asn_DEF_cost_3 = {
	"cost",
	"cost",
	&asn_CHOICE_cost_3,
	asn_DEF_cost_tags_3,
	sizeof(asn_DEF_cost_tags_3)
		/sizeof(asn_DEF_cost_tags_3[0]) - 1, /* 1 */
	asn_DEF_cost_tags_3,	/* Same as above */
	sizeof(asn_DEF_cost_tags_3)
		/sizeof(asn_DEF_cost_tags_3[0]), /* 2 */
	{ 0, 0, cost_3_constraint },
	0,
	0,
	&asn_SPC_cost_specs_3	/* Additional specs */
};

/*	
	cost_3_free,
	cost_3_print,
	cost_3_constraint,
	cost_3_decode_ber,
	cost_3_encode_der,
	cost_3_decode_xer,
	cost_3_encode_xer,
	0, 0,	//No PER support, use "-gen-PER" to enable 
	0,	//Use generic outmost tag fetcher 
*/


static asn_TYPE_member_t asn_MBR_SimpleSha256Condition_1[] = {
	{ ATF_NOFLAGS, 0, offsetof(struct SimpleSha256Condition, fingerprint),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_OCTET_STRING,
		0, /* IoS runtime type selector */
		{0,0, memb_fingerprint_constraint_1},
		0,
		0,
		"fingerprint"
		},
	{ ATF_NOFLAGS, 0, offsetof(struct SimpleSha256Condition, cost),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_cost_3,
		0, /* IoS runtime type selector */
		{0,0, memb_cost_constraint_1},
		0, /* Compare DEFAULT <value> */
		0, /* Set DEFAULT <value> */
		"cost"
		},
};
static const ber_tlv_tag_t asn_DEF_SimpleSha256Condition_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_SimpleSha256Condition_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* fingerprint */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 } /* cost */
};
static asn_SEQUENCE_specifics_t asn_SPC_SimpleSha256Condition_specs_1 = {
	sizeof(struct SimpleSha256Condition),
	offsetof(struct SimpleSha256Condition, _asn_ctx),
	asn_MAP_SimpleSha256Condition_tag2el_1,
	2,	/* Count of tags in the map */
	0, 0, 0,	/* Optional elements (not needed) */
	-1	/* Start extensions */
//-1	/* Stop extensions */
};

static asn_TYPE_operation_t asn_CHOICE_SimpleSha256Condition = {
	SEQUENCE_free,
	SEQUENCE_print,
	SEQUENCE_compare,
	SEQUENCE_decode_ber,
	SEQUENCE_encode_der,
	SEQUENCE_decode_xer,
	SEQUENCE_encode_xer,
	0,
	0,
	0,
	0,
	0,
	0
};


asn_TYPE_descriptor_t asn_DEF_SimpleSha256Condition = {
	"SimpleSha256Condition",
	"SimpleSha256Condition",
	&asn_CHOICE_SimpleSha256Condition,
	asn_DEF_SimpleSha256Condition_tags_1,
	sizeof(asn_DEF_SimpleSha256Condition_tags_1)
		/sizeof(asn_DEF_SimpleSha256Condition_tags_1[0]), /* 1 */
	asn_DEF_SimpleSha256Condition_tags_1,	/* Same as above */
	sizeof(asn_DEF_SimpleSha256Condition_tags_1)
		/sizeof(asn_DEF_SimpleSha256Condition_tags_1[0]), /* 1 */
	{ 0, 0, SEQUENCE_constraint },
	asn_MBR_SimpleSha256Condition_1,
	2,	/* Elements count */
	&asn_SPC_SimpleSha256Condition_specs_1	/* Additional specs */
};

