/* dwarf.c -- display DWARF contents of a BFD binary file
   Copyright 2005, 2006, 2007, 2008, 2009
   Free Software Foundation, Inc.

   This file is part of GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "libiberty.h"
#include "bfd.h"
#include "bucomm.h"
#include "elf/common.h"
#include "dwarf2.h"
#include "dwarf.h"

static int have_frame_base;
static int need_base_address;

static unsigned int last_pointer_size = 0;
static int warned_about_missing_comp_units = FALSE;

static unsigned int num_debug_info_entries = 0;
static debug_info *debug_information = NULL;
/* Special value for num_debug_info_entries to indicate
   that the .debug_info section could not be loaded/parsed.  */
#define DEBUG_INFO_UNAVAILABLE  (unsigned int) -1

int eh_addr_size;

int do_debug_info;
int do_debug_abbrevs;
int do_debug_lines;
int do_debug_pubnames;
int do_debug_aranges;
int do_debug_ranges;
int do_debug_frames;
int do_debug_frames_interp;
int do_debug_macinfo;
int do_debug_str;
int do_debug_loc;
int do_wide;

/* Values for do_debug_lines.  */
#define FLAG_DEBUG_LINES_RAW	 1
#define FLAG_DEBUG_LINES_DECODED 2

dwarf_vma (*byte_get) (unsigned char *, int);

dwarf_vma
byte_get_little_endian (unsigned char *field, int size)
{
  switch (size)
    {
    case 1:
      return *field;

    case 2:
      return  ((unsigned int) (field[0]))
	|    (((unsigned int) (field[1])) << 8);

    case 3:
      return  ((unsigned long) (field[0]))
	|    (((unsigned long) (field[1])) << 8)
	|    (((unsigned long) (field[2])) << 16);

    case 4:
      return  ((unsigned long) (field[0]))
	|    (((unsigned long) (field[1])) << 8)
	|    (((unsigned long) (field[2])) << 16)
	|    (((unsigned long) (field[3])) << 24);

    case 8:
      if (sizeof (dwarf_vma) == 8)
	return  ((dwarf_vma) (field[0]))
	  |    (((dwarf_vma) (field[1])) << 8)
	  |    (((dwarf_vma) (field[2])) << 16)
	  |    (((dwarf_vma) (field[3])) << 24)
	  |    (((dwarf_vma) (field[4])) << 32)
	  |    (((dwarf_vma) (field[5])) << 40)
	  |    (((dwarf_vma) (field[6])) << 48)
	  |    (((dwarf_vma) (field[7])) << 56);
      else if (sizeof (dwarf_vma) == 4)
	/* We want to extract data from an 8 byte wide field and
	   place it into a 4 byte wide field.  Since this is a little
	   endian source we can just use the 4 byte extraction code.  */
	return  ((unsigned long) (field[0]))
	  |    (((unsigned long) (field[1])) << 8)
	  |    (((unsigned long) (field[2])) << 16)
	  |    (((unsigned long) (field[3])) << 24);

    default:
      error (_("Unhandled data length: %d\n"), size);
      abort ();
    }
}

dwarf_vma
byte_get_big_endian (unsigned char *field, int size)
{
  switch (size)
    {
    case 1:
      return *field;

    case 2:
      return ((unsigned int) (field[1])) | (((int) (field[0])) << 8);

    case 3:
      return ((unsigned long) (field[2]))
	|   (((unsigned long) (field[1])) << 8)
	|   (((unsigned long) (field[0])) << 16);

    case 4:
      return ((unsigned long) (field[3]))
	|   (((unsigned long) (field[2])) << 8)
	|   (((unsigned long) (field[1])) << 16)
	|   (((unsigned long) (field[0])) << 24);

    case 8:
      if (sizeof (dwarf_vma) == 8)
	return ((dwarf_vma) (field[7]))
	  |   (((dwarf_vma) (field[6])) << 8)
	  |   (((dwarf_vma) (field[5])) << 16)
	  |   (((dwarf_vma) (field[4])) << 24)
	  |   (((dwarf_vma) (field[3])) << 32)
	  |   (((dwarf_vma) (field[2])) << 40)
	  |   (((dwarf_vma) (field[1])) << 48)
	  |   (((dwarf_vma) (field[0])) << 56);
      else if (sizeof (dwarf_vma) == 4)
	{
	  /* Although we are extracing data from an 8 byte wide field,
	     we are returning only 4 bytes of data.  */
	  field += 4;
	  return ((unsigned long) (field[3]))
	    |   (((unsigned long) (field[2])) << 8)
	    |   (((unsigned long) (field[1])) << 16)
	    |   (((unsigned long) (field[0])) << 24);
	}

    default:
      error (_("Unhandled data length: %d\n"), size);
      abort ();
    }
}

static dwarf_vma
byte_get_signed (unsigned char *field, int size)
{
  dwarf_vma x = byte_get (field, size);

  switch (size)
    {
    case 1:
      return (x ^ 0x80) - 0x80;
    case 2:
      return (x ^ 0x8000) - 0x8000;
    case 4:
      return (x ^ 0x80000000) - 0x80000000;
    case 8:
      return x;
    default:
      abort ();
    }
}

static int
size_of_encoded_value (int encoding)
{
  switch (encoding & 0x7)
    {
    default:	/* ??? */
    case 0:	return eh_addr_size;
    case 2:	return 2;
    case 3:	return 4;
    case 4:	return 8;
    }
}

static dwarf_vma
get_encoded_value (unsigned char *data, int encoding)
{
  int size = size_of_encoded_value (encoding);

  if (encoding & DW_EH_PE_signed)
    return byte_get_signed (data, size);
  else
    return byte_get (data, size);
}

/* Print a dwarf_vma value (typically an address, offset or length) in
   hexadecimal format, followed by a space.  The length of the value (and
   hence the precision displayed) is determined by the byte_size parameter.  */

static void
print_dwarf_vma (dwarf_vma val, unsigned byte_size)
{
  static char buff[18];

  /* Printf does not have a way of specifiying a maximum field width for an
     integer value, so we print the full value into a buffer and then select
     the precision we need.  */
#if __STDC_VERSION__ >= 199901L || (defined(__GNUC__) && __GNUC__ >= 2)
#ifndef __MSVCRT__
  snprintf (buff, sizeof (buff), "%16.16llx ", val);
#else
  snprintf (buff, sizeof (buff), "%016I64x ", val);
#endif
#else
  snprintf (buff, sizeof (buff), "%16.16lx ", val);
#endif

  fputs (buff + (byte_size == 4 ? 8 : 0), stdout);
}

static unsigned long int
read_leb128 (unsigned char *data, unsigned int *length_return, int sign)
{
  unsigned long int result = 0;
  unsigned int num_read = 0;
  unsigned int shift = 0;
  unsigned char byte;

  do
    {
      byte = *data++;
      num_read++;

      result |= ((unsigned long int) (byte & 0x7f)) << shift;

      shift += 7;

    }
  while (byte & 0x80);

  if (length_return != NULL)
    *length_return = num_read;

  if (sign && (shift < 8 * sizeof (result)) && (byte & 0x40))
    result |= -1L << shift;

  return result;
}

typedef struct State_Machine_Registers
{
  unsigned long address;
  unsigned int file;
  unsigned int line;
  unsigned int column;
  int is_stmt;
  int basic_block;
  int end_sequence;
/* This variable hold the number of the last entry seen
   in the File Table.  */
  unsigned int last_file_entry;
} SMR;

static SMR state_machine_regs;

static void
reset_state_machine (int is_stmt)
{
  state_machine_regs.address = 0;
  state_machine_regs.file = 1;
  state_machine_regs.line = 1;
  state_machine_regs.column = 0;
  state_machine_regs.is_stmt = is_stmt;
  state_machine_regs.basic_block = 0;
  state_machine_regs.end_sequence = 0;
  state_machine_regs.last_file_entry = 0;
}

/* Handled an extend line op.
   Returns the number of bytes read.  */

static int
process_extended_line_op (unsigned char *data, int is_stmt)
{
  unsigned char op_code;
  unsigned int bytes_read;
  unsigned int len;
  unsigned char *name;
  unsigned long adr;

  len = read_leb128 (data, & bytes_read, 0);
  data += bytes_read;

  if (len == 0)
    {
      warn (_("badly formed extended line op encountered!\n"));
      return bytes_read;
    }

  len += bytes_read;
  op_code = *data++;

  printf (_("  Extended opcode %d: "), op_code);

  switch (op_code)
    {
    case DW_LNE_end_sequence:
      printf (_("End of Sequence\n\n"));
      reset_state_machine (is_stmt);
      break;

    case DW_LNE_set_address:
      adr = byte_get (data, len - bytes_read - 1);
      printf (_("set Address to 0x%lx\n"), adr);
      state_machine_regs.address = adr;
      break;

    case DW_LNE_define_file:
      printf (_("  define new File Table entry\n"));
      printf (_("  Entry\tDir\tTime\tSize\tName\n"));

      printf (_("   %d\t"), ++state_machine_regs.last_file_entry);
      name = data;
      data += strlen ((char *) data) + 1;
      printf (_("%lu\t"), read_leb128 (data, & bytes_read, 0));
      data += bytes_read;
      printf (_("%lu\t"), read_leb128 (data, & bytes_read, 0));
      data += bytes_read;
      printf (_("%lu\t"), read_leb128 (data, & bytes_read, 0));
      printf (_("%s\n\n"), name);
      break;

    case DW_LNE_set_discriminator:
      printf (_("set Discriminator to %lu\n"),
              read_leb128 (data, & bytes_read, 0));
      break;

    /* HP extensions.  */
    case DW_LNE_HP_negate_is_UV_update:
      printf ("DW_LNE_HP_negate_is_UV_update\n");
      break;
    case DW_LNE_HP_push_context:
      printf ("DW_LNE_HP_push_context\n");
      break;
    case DW_LNE_HP_pop_context:
      printf ("DW_LNE_HP_pop_context\n");
      break;
    case DW_LNE_HP_set_file_line_column:
      printf ("DW_LNE_HP_set_file_line_column\n");
      break;
    case DW_LNE_HP_set_routine_name:
      printf ("DW_LNE_HP_set_routine_name\n");
      break;
    case DW_LNE_HP_set_sequence:
      printf ("DW_LNE_HP_set_sequence\n");
      break;
    case DW_LNE_HP_negate_post_semantics:
      printf ("DW_LNE_HP_negate_post_semantics\n");
      break;
    case DW_LNE_HP_negate_function_exit:
      printf ("DW_LNE_HP_negate_function_exit\n");
      break;
    case DW_LNE_HP_negate_front_end_logical:
      printf ("DW_LNE_HP_negate_front_end_logical\n");
      break;
    case DW_LNE_HP_define_proc:
      printf ("DW_LNE_HP_define_proc\n");
      break;

    default:
      if (op_code >= DW_LNE_lo_user
	  /* The test against DW_LNW_hi_user is redundant due to
	     the limited range of the unsigned char data type used
	     for op_code.  */
	  /*&& op_code <= DW_LNE_hi_user*/)
	printf (_("user defined: length %d\n"), len - bytes_read);
      else
	printf (_("UNKNOWN: length %d\n"), len - bytes_read);
      break;
    }

  return len;
}

static const char *
fetch_indirect_string (unsigned long offset)
{
  struct dwarf_section *section = &debug_displays [str].section;

  if (section->start == NULL)
    return _("<no .debug_str section>");

  /* DWARF sections under Mach-O have non-zero addresses.  */
  offset -= section->address;
  if (offset > section->size)
    {
      warn (_("DW_FORM_strp offset too big: %lx\n"), offset);
      return _("<offset is too big>");
    }

  return (const char *) section->start + offset;
}

/* FIXME:  There are better and more efficient ways to handle
   these structures.  For now though, I just want something that
   is simple to implement.  */
typedef struct abbrev_attr
{
  unsigned long attribute;
  unsigned long form;
  struct abbrev_attr *next;
}
abbrev_attr;

typedef struct abbrev_entry
{
  unsigned long entry;
  unsigned long tag;
  int children;
  struct abbrev_attr *first_attr;
  struct abbrev_attr *last_attr;
  struct abbrev_entry *next;
}
abbrev_entry;

static abbrev_entry *first_abbrev = NULL;
static abbrev_entry *last_abbrev = NULL;

static void
free_abbrevs (void)
{
  abbrev_entry *abbrev;

  for (abbrev = first_abbrev; abbrev;)
    {
      abbrev_entry *next = abbrev->next;
      abbrev_attr *attr;

      for (attr = abbrev->first_attr; attr;)
	{
	  abbrev_attr *next = attr->next;

	  free (attr);
	  attr = next;
	}

      free (abbrev);
      abbrev = next;
    }

  last_abbrev = first_abbrev = NULL;
}

static void
add_abbrev (unsigned long number, unsigned long tag, int children)
{
  abbrev_entry *entry;

  entry = (abbrev_entry *) malloc (sizeof (*entry));

  if (entry == NULL)
    /* ugg */
    return;

  entry->entry      = number;
  entry->tag        = tag;
  entry->children   = children;
  entry->first_attr = NULL;
  entry->last_attr  = NULL;
  entry->next       = NULL;

  if (first_abbrev == NULL)
    first_abbrev = entry;
  else
    last_abbrev->next = entry;

  last_abbrev = entry;
}

static void
add_abbrev_attr (unsigned long attribute, unsigned long form)
{
  abbrev_attr *attr;

  attr = (abbrev_attr *) malloc (sizeof (*attr));

  if (attr == NULL)
    /* ugg */
    return;

  attr->attribute = attribute;
  attr->form      = form;
  attr->next      = NULL;

  if (last_abbrev->first_attr == NULL)
    last_abbrev->first_attr = attr;
  else
    last_abbrev->last_attr->next = attr;

  last_abbrev->last_attr = attr;
}

/* Processes the (partial) contents of a .debug_abbrev section.
   Returns NULL if the end of the section was encountered.
   Returns the address after the last byte read if the end of
   an abbreviation set was found.  */

static unsigned char *
process_abbrev_section (unsigned char *start, unsigned char *end)
{
  if (first_abbrev != NULL)
    return NULL;

  while (start < end)
    {
      unsigned int bytes_read;
      unsigned long entry;
      unsigned long tag;
      unsigned long attribute;
      int children;

      entry = read_leb128 (start, & bytes_read, 0);
      start += bytes_read;

      /* A single zero is supposed to end the section according
	 to the standard.  If there's more, then signal that to
	 the caller.  */
      if (entry == 0)
	return start == end ? NULL : start;

      tag = read_leb128 (start, & bytes_read, 0);
      start += bytes_read;

      children = *start++;

      add_abbrev (entry, tag, children);

      do
	{
	  unsigned long form;

	  attribute = read_leb128 (start, & bytes_read, 0);
	  start += bytes_read;

	  form = read_leb128 (start, & bytes_read, 0);
	  start += bytes_read;

	  if (attribute != 0)
	    add_abbrev_attr (attribute, form);
	}
      while (attribute != 0);
    }

  return NULL;
}

static char *
get_TAG_name (unsigned long tag)
{
  switch (tag)
    {
    case DW_TAG_padding:		return "DW_TAG_padding";
    case DW_TAG_array_type:		return "DW_TAG_array_type";
    case DW_TAG_class_type:		return "DW_TAG_class_type";
    case DW_TAG_entry_point:		return "DW_TAG_entry_point";
    case DW_TAG_enumeration_type:	return "DW_TAG_enumeration_type";
    case DW_TAG_formal_parameter:	return "DW_TAG_formal_parameter";
    case DW_TAG_imported_declaration:	return "DW_TAG_imported_declaration";
    case DW_TAG_label:			return "DW_TAG_label";
    case DW_TAG_lexical_block:		return "DW_TAG_lexical_block";
    case DW_TAG_member:			return "DW_TAG_member";
    case DW_TAG_pointer_type:		return "DW_TAG_pointer_type";
    case DW_TAG_reference_type:		return "DW_TAG_reference_type";
    case DW_TAG_compile_unit:		return "DW_TAG_compile_unit";
    case DW_TAG_string_type:		return "DW_TAG_string_type";
    case DW_TAG_structure_type:		return "DW_TAG_structure_type";
    case DW_TAG_subroutine_type:	return "DW_TAG_subroutine_type";
    case DW_TAG_typedef:		return "DW_TAG_typedef";
    case DW_TAG_union_type:		return "DW_TAG_union_type";
    case DW_TAG_unspecified_parameters: return "DW_TAG_unspecified_parameters";
    case DW_TAG_variant:		return "DW_TAG_variant";
    case DW_TAG_common_block:		return "DW_TAG_common_block";
    case DW_TAG_common_inclusion:	return "DW_TAG_common_inclusion";
    case DW_TAG_inheritance:		return "DW_TAG_inheritance";
    case DW_TAG_inlined_subroutine:	return "DW_TAG_inlined_subroutine";
    case DW_TAG_module:			return "DW_TAG_module";
    case DW_TAG_ptr_to_member_type:	return "DW_TAG_ptr_to_member_type";
    case DW_TAG_set_type:		return "DW_TAG_set_type";
    case DW_TAG_subrange_type:		return "DW_TAG_subrange_type";
    case DW_TAG_with_stmt:		return "DW_TAG_with_stmt";
    case DW_TAG_access_declaration:	return "DW_TAG_access_declaration";
    case DW_TAG_base_type:		return "DW_TAG_base_type";
    case DW_TAG_catch_block:		return "DW_TAG_catch_block";
    case DW_TAG_const_type:		return "DW_TAG_const_type";
    case DW_TAG_constant:		return "DW_TAG_constant";
    case DW_TAG_enumerator:		return "DW_TAG_enumerator";
    case DW_TAG_file_type:		return "DW_TAG_file_type";
    case DW_TAG_friend:			return "DW_TAG_friend";
    case DW_TAG_namelist:		return "DW_TAG_namelist";
    case DW_TAG_namelist_item:		return "DW_TAG_namelist_item";
    case DW_TAG_packed_type:		return "DW_TAG_packed_type";
    case DW_TAG_subprogram:		return "DW_TAG_subprogram";
    case DW_TAG_template_type_param:	return "DW_TAG_template_type_param";
    case DW_TAG_template_value_param:	return "DW_TAG_template_value_param";
    case DW_TAG_thrown_type:		return "DW_TAG_thrown_type";
    case DW_TAG_try_block:		return "DW_TAG_try_block";
    case DW_TAG_variant_part:		return "DW_TAG_variant_part";
    case DW_TAG_variable:		return "DW_TAG_variable";
    case DW_TAG_volatile_type:		return "DW_TAG_volatile_type";
    case DW_TAG_MIPS_loop:		return "DW_TAG_MIPS_loop";
    case DW_TAG_format_label:		return "DW_TAG_format_label";
    case DW_TAG_function_template:	return "DW_TAG_function_template";
    case DW_TAG_class_template:		return "DW_TAG_class_template";
      /* DWARF 2.1 values.  */
    case DW_TAG_dwarf_procedure:	return "DW_TAG_dwarf_procedure";
    case DW_TAG_restrict_type:		return "DW_TAG_restrict_type";
    case DW_TAG_interface_type:		return "DW_TAG_interface_type";
    case DW_TAG_namespace:		return "DW_TAG_namespace";
    case DW_TAG_imported_module:	return "DW_TAG_imported_module";
    case DW_TAG_unspecified_type:	return "DW_TAG_unspecified_type";
    case DW_TAG_partial_unit:		return "DW_TAG_partial_unit";
    case DW_TAG_imported_unit:		return "DW_TAG_imported_unit";
    case DW_TAG_condition:		return "DW_TAG_condition";
    case DW_TAG_shared_type:		return "DW_TAG_shared_type";
      /* DWARF 4 values.  */
    case DW_TAG_type_unit:		return "DW_TAG_type_unit";
    case DW_TAG_rvalue_reference_type:	return "DW_TAG_rvalue_reference_type";
    case DW_TAG_template_alias:		return "DW_TAG_template_alias";
      /* UPC values.  */
    case DW_TAG_upc_shared_type:	return "DW_TAG_upc_shared_type";
    case DW_TAG_upc_strict_type:	return "DW_TAG_upc_strict_type";
    case DW_TAG_upc_relaxed_type:	return "DW_TAG_upc_relaxed_type";
    default:
      {
	static char buffer[100];

	snprintf (buffer, sizeof (buffer), _("Unknown TAG value: %lx"), tag);
	return buffer;
      }
    }
}

static char *
get_FORM_name (unsigned long form)
{
  switch (form)
    {
    case DW_FORM_addr:		return "DW_FORM_addr";
    case DW_FORM_block2:	return "DW_FORM_block2";
    case DW_FORM_block4:	return "DW_FORM_block4";
    case DW_FORM_data2:		return "DW_FORM_data2";
    case DW_FORM_data4:		return "DW_FORM_data4";
    case DW_FORM_data8:		return "DW_FORM_data8";
    case DW_FORM_string:	return "DW_FORM_string";
    case DW_FORM_block:		return "DW_FORM_block";
    case DW_FORM_block1:	return "DW_FORM_block1";
    case DW_FORM_data1:		return "DW_FORM_data1";
    case DW_FORM_flag:		return "DW_FORM_flag";
    case DW_FORM_sdata:		return "DW_FORM_sdata";
    case DW_FORM_strp:		return "DW_FORM_strp";
    case DW_FORM_udata:		return "DW_FORM_udata";
    case DW_FORM_ref_addr:	return "DW_FORM_ref_addr";
    case DW_FORM_ref1:		return "DW_FORM_ref1";
    case DW_FORM_ref2:		return "DW_FORM_ref2";
    case DW_FORM_ref4:		return "DW_FORM_ref4";
    case DW_FORM_ref8:		return "DW_FORM_ref8";
    case DW_FORM_ref_udata:	return "DW_FORM_ref_udata";
    case DW_FORM_indirect:	return "DW_FORM_indirect";
      /* DWARF 4 values.  */
    case DW_FORM_sec_offset:	return "DW_FORM_sec_offset";
    case DW_FORM_exprloc:	return "DW_FORM_exprloc";
    case DW_FORM_flag_present:	return "DW_FORM_flag_present";
    case DW_FORM_ref_sig8:	return "DW_FORM_ref_sig8";
    default:
      {
	static char buffer[100];

	snprintf (buffer, sizeof (buffer), _("Unknown FORM value: %lx"), form);
	return buffer;
      }
    }
}

static unsigned char *
display_block (unsigned char *data, unsigned long length)
{
  printf (_(" %lu byte block: "), length);

  while (length --)
    printf ("%lx ", (unsigned long) byte_get (data++, 1));

  return data;
}

static int
decode_location_expression (unsigned char * data,
			    unsigned int pointer_size,
			    unsigned long length,
			    unsigned long cu_offset,
			    struct dwarf_section * section)
{
  unsigned op;
  unsigned int bytes_read;
  unsigned long uvalue;
  unsigned char *end = data + length;
  int need_frame_base = 0;

  while (data < end)
    {
      op = *data++;

      switch (op)
	{
	case DW_OP_addr:
	  printf ("DW_OP_addr: %lx",
		  (unsigned long) byte_get (data, pointer_size));
	  data += pointer_size;
	  break;
	case DW_OP_deref:
	  printf ("DW_OP_deref");
	  break;
	case DW_OP_const1u:
	  printf ("DW_OP_const1u: %lu", (unsigned long) byte_get (data++, 1));
	  break;
	case DW_OP_const1s:
	  printf ("DW_OP_const1s: %ld", (long) byte_get_signed (data++, 1));
	  break;
	case DW_OP_const2u:
	  printf ("DW_OP_const2u: %lu", (unsigned long) byte_get (data, 2));
	  data += 2;
	  break;
	case DW_OP_const2s:
	  printf ("DW_OP_const2s: %ld", (long) byte_get_signed (data, 2));
	  data += 2;
	  break;
	case DW_OP_const4u:
	  printf ("DW_OP_const4u: %lu", (unsigned long) byte_get (data, 4));
	  data += 4;
	  break;
	case DW_OP_const4s:
	  printf ("DW_OP_const4s: %ld", (long) byte_get_signed (data, 4));
	  data += 4;
	  break;
	case DW_OP_const8u:
	  printf ("DW_OP_const8u: %lu %lu", (unsigned long) byte_get (data, 4),
		  (unsigned long) byte_get (data + 4, 4));
	  data += 8;
	  break;
	case DW_OP_const8s:
	  printf ("DW_OP_const8s: %ld %ld", (long) byte_get (data, 4),
		  (long) byte_get (data + 4, 4));
	  data += 8;
	  break;
	case DW_OP_constu:
	  printf ("DW_OP_constu: %lu", read_leb128 (data, &bytes_read, 0));
	  data += bytes_read;
	  break;
	case DW_OP_consts:
	  printf ("DW_OP_consts: %ld", read_leb128 (data, &bytes_read, 1));
	  data += bytes_read;
	  break;
	case DW_OP_dup:
	  printf ("DW_OP_dup");
	  break;
	case DW_OP_drop:
	  printf ("DW_OP_drop");
	  break;
	case DW_OP_over:
	  printf ("DW_OP_over");
	  break;
	case DW_OP_pick:
	  printf ("DW_OP_pick: %ld", (unsigned long) byte_get (data++, 1));
	  break;
	case DW_OP_swap:
	  printf ("DW_OP_swap");
	  break;
	case DW_OP_rot:
	  printf ("DW_OP_rot");
	  break;
	case DW_OP_xderef:
	  printf ("DW_OP_xderef");
	  break;
	case DW_OP_abs:
	  printf ("DW_OP_abs");
	  break;
	case DW_OP_and:
	  printf ("DW_OP_and");
	  break;
	case DW_OP_div:
	  printf ("DW_OP_div");
	  break;
	case DW_OP_minus:
	  printf ("DW_OP_minus");
	  break;
	case DW_OP_mod:
	  printf ("DW_OP_mod");
	  break;
	case DW_OP_mul:
	  printf ("DW_OP_mul");
	  break;
	case DW_OP_neg:
	  printf ("DW_OP_neg");
	  break;
	case DW_OP_not:
	  printf ("DW_OP_not");
	  break;
	case DW_OP_or:
	  printf ("DW_OP_or");
	  break;
	case DW_OP_plus:
	  printf ("DW_OP_plus");
	  break;
	case DW_OP_plus_uconst:
	  printf ("DW_OP_plus_uconst: %lu",
		  read_leb128 (data, &bytes_read, 0));
	  data += bytes_read;
	  break;
	case DW_OP_shl:
	  printf ("DW_OP_shl");
	  break;
	case DW_OP_shr:
	  printf ("DW_OP_shr");
	  break;
	case DW_OP_shra:
	  printf ("DW_OP_shra");
	  break;
	case DW_OP_xor:
	  printf ("DW_OP_xor");
	  break;
	case DW_OP_bra:
	  printf ("DW_OP_bra: %ld", (long) byte_get_signed (data, 2));
	  data += 2;
	  break;
	case DW_OP_eq:
	  printf ("DW_OP_eq");
	  break;
	case DW_OP_ge:
	  printf ("DW_OP_ge");
	  break;
	case DW_OP_gt:
	  printf ("DW_OP_gt");
	  break;
	case DW_OP_le:
	  printf ("DW_OP_le");
	  break;
	case DW_OP_lt:
	  printf ("DW_OP_lt");
	  break;
	case DW_OP_ne:
	  printf ("DW_OP_ne");
	  break;
	case DW_OP_skip:
	  printf ("DW_OP_skip: %ld", (long) byte_get_signed (data, 2));
	  data += 2;
	  break;

	case DW_OP_lit0:
	case DW_OP_lit1:
	case DW_OP_lit2:
	case DW_OP_lit3:
	case DW_OP_lit4:
	case DW_OP_lit5:
	case DW_OP_lit6:
	case DW_OP_lit7:
	case DW_OP_lit8:
	case DW_OP_lit9:
	case DW_OP_lit10:
	case DW_OP_lit11:
	case DW_OP_lit12:
	case DW_OP_lit13:
	case DW_OP_lit14:
	case DW_OP_lit15:
	case DW_OP_lit16:
	case DW_OP_lit17:
	case DW_OP_lit18:
	case DW_OP_lit19:
	case DW_OP_lit20:
	case DW_OP_lit21:
	case DW_OP_lit22:
	case DW_OP_lit23:
	case DW_OP_lit24:
	case DW_OP_lit25:
	case DW_OP_lit26:
	case DW_OP_lit27:
	case DW_OP_lit28:
	case DW_OP_lit29:
	case DW_OP_lit30:
	case DW_OP_lit31:
	  printf ("DW_OP_lit%d", op - DW_OP_lit0);
	  break;

	case DW_OP_reg0:
	case DW_OP_reg1:
	case DW_OP_reg2:
	case DW_OP_reg3:
	case DW_OP_reg4:
	case DW_OP_reg5:
	case DW_OP_reg6:
	case DW_OP_reg7:
	case DW_OP_reg8:
	case DW_OP_reg9:
	case DW_OP_reg10:
	case DW_OP_reg11:
	case DW_OP_reg12:
	case DW_OP_reg13:
	case DW_OP_reg14:
	case DW_OP_reg15:
	case DW_OP_reg16:
	case DW_OP_reg17:
	case DW_OP_reg18:
	case DW_OP_reg19:
	case DW_OP_reg20:
	case DW_OP_reg21:
	case DW_OP_reg22:
	case DW_OP_reg23:
	case DW_OP_reg24:
	case DW_OP_reg25:
	case DW_OP_reg26:
	case DW_OP_reg27:
	case DW_OP_reg28:
	case DW_OP_reg29:
	case DW_OP_reg30:
	case DW_OP_reg31:
	  printf ("DW_OP_reg%d", op - DW_OP_reg0);
	  break;

	case DW_OP_breg0:
	case DW_OP_breg1:
	case DW_OP_breg2:
	case DW_OP_breg3:
	case DW_OP_breg4:
	case DW_OP_breg5:
	case DW_OP_breg6:
	case DW_OP_breg7:
	case DW_OP_breg8:
	case DW_OP_breg9:
	case DW_OP_breg10:
	case DW_OP_breg11:
	case DW_OP_breg12:
	case DW_OP_breg13:
	case DW_OP_breg14:
	case DW_OP_breg15:
	case DW_OP_breg16:
	case DW_OP_breg17:
	case DW_OP_breg18:
	case DW_OP_breg19:
	case DW_OP_breg20:
	case DW_OP_breg21:
	case DW_OP_breg22:
	case DW_OP_breg23:
	case DW_OP_breg24:
	case DW_OP_breg25:
	case DW_OP_breg26:
	case DW_OP_breg27:
	case DW_OP_breg28:
	case DW_OP_breg29:
	case DW_OP_breg30:
	case DW_OP_breg31:
	  printf ("DW_OP_breg%d: %ld", op - DW_OP_breg0,
		  read_leb128 (data, &bytes_read, 1));
	  data += bytes_read;
	  break;

	case DW_OP_regx:
	  printf ("DW_OP_regx: %lu", read_leb128 (data, &bytes_read, 0));
	  data += bytes_read;
	  break;
	case DW_OP_fbreg:
	  need_frame_base = 1;
	  printf ("DW_OP_fbreg: %ld", read_leb128 (data, &bytes_read, 1));
	  data += bytes_read;
	  break;
	case DW_OP_bregx:
	  uvalue = read_leb128 (data, &bytes_read, 0);
	  data += bytes_read;
	  printf ("DW_OP_bregx: %lu %ld", uvalue,
		  read_leb128 (data, &bytes_read, 1));
	  data += bytes_read;
	  break;
	case DW_OP_piece:
	  printf ("DW_OP_piece: %lu", read_leb128 (data, &bytes_read, 0));
	  data += bytes_read;
	  break;
	case DW_OP_deref_size:
	  printf ("DW_OP_deref_size: %ld", (long) byte_get (data++, 1));
	  break;
	case DW_OP_xderef_size:
	  printf ("DW_OP_xderef_size: %ld", (long) byte_get (data++, 1));
	  break;
	case DW_OP_nop:
	  printf ("DW_OP_nop");
	  break;

	  /* DWARF 3 extensions.  */
	case DW_OP_push_object_address:
	  printf ("DW_OP_push_object_address");
	  break;
	case DW_OP_call2:
	  /* XXX: Strictly speaking for 64-bit DWARF3 files
	     this ought to be an 8-byte wide computation.  */
	  printf ("DW_OP_call2: <%lx>", (long) byte_get (data, 2) + cu_offset);
	  data += 2;
	  break;
	case DW_OP_call4:
	  /* XXX: Strictly speaking for 64-bit DWARF3 files
	     this ought to be an 8-byte wide computation.  */
	  printf ("DW_OP_call4: <%lx>", (long) byte_get (data, 4) + cu_offset);
	  data += 4;
	  break;
	case DW_OP_call_ref:
	  /* XXX: Strictly speaking for 64-bit DWARF3 files
	     this ought to be an 8-byte wide computation.  */
	  printf ("DW_OP_call_ref: <%lx>", (long) byte_get (data, 4) + cu_offset);
	  data += 4;
	  break;
	case DW_OP_form_tls_address:
	  printf ("DW_OP_form_tls_address");
	  break;
	case DW_OP_call_frame_cfa:
	  printf ("DW_OP_call_frame_cfa");
	  break;
	case DW_OP_bit_piece:
	  printf ("DW_OP_bit_piece: ");
	  printf ("size: %lu ", read_leb128 (data, &bytes_read, 0));
	  data += bytes_read;
	  printf ("offset: %lu ", read_leb128 (data, &bytes_read, 0));
	  data += bytes_read;
	  break;

	  /* DWARF 4 extensions.  */
	case DW_OP_stack_value:
	  printf ("DW_OP_stack_value");
	  break;

	case DW_OP_implicit_value:
	  printf ("DW_OP_implicit_value");
	  uvalue = read_leb128 (data, &bytes_read, 0);
	  data += bytes_read;
	  display_block (data, uvalue);
	  data += uvalue;
	  break;

	  /* GNU extensions.  */
	case DW_OP_GNU_push_tls_address:
	  printf ("DW_OP_GNU_push_tls_address or DW_OP_HP_unknown");
	  break;
	case DW_OP_GNU_uninit:
	  printf ("DW_OP_GNU_uninit");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;
	case DW_OP_GNU_encoded_addr:
	  {
	    int encoding;
	    dwarf_vma addr;
	
	    encoding = *data++;
	    addr = get_encoded_value (data, encoding);
	    if ((encoding & 0x70) == DW_EH_PE_pcrel)
	      addr += section->address + (data - section->start);
	    data += size_of_encoded_value (encoding);

	    printf ("DW_OP_GNU_encoded_addr: fmt:%02x addr:", encoding);
	    print_dwarf_vma (addr, pointer_size);
	  }
	  break;

	  /* HP extensions.  */
	case DW_OP_HP_is_value:
	  printf ("DW_OP_HP_is_value");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;
	case DW_OP_HP_fltconst4:
	  printf ("DW_OP_HP_fltconst4");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;
	case DW_OP_HP_fltconst8:
	  printf ("DW_OP_HP_fltconst8");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;
	case DW_OP_HP_mod_range:
	  printf ("DW_OP_HP_mod_range");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;
	case DW_OP_HP_unmod_range:
	  printf ("DW_OP_HP_unmod_range");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;
	case DW_OP_HP_tls:
	  printf ("DW_OP_HP_tls");
	  /* FIXME: Is there data associated with this OP ?  */
	  break;

	  /* PGI (STMicroelectronics) extensions.  */
	case DW_OP_PGI_omp_thread_num:
	  /* Pushes the thread number for the current thread as it would be
	     returned by the standard OpenMP library function:
	     omp_get_thread_num().  The "current thread" is the thread for
	     which the expression is being evaluated.  */
	  printf ("DW_OP_PGI_omp_thread_num");
	  break;

	default:
	  if (op >= DW_OP_lo_user
	      && op <= DW_OP_hi_user)
	    printf (_("(User defined location op)"));
	  else
	    printf (_("(Unknown location op)"));
	  /* No way to tell where the next op is, so just bail.  */
	  return need_frame_base;
	}

      /* Separate the ops.  */
      if (data < end)
	printf ("; ");
    }

  return need_frame_base;
}

static unsigned char *
read_and_display_attr_value (unsigned long attribute,
			     unsigned long form,
			     unsigned char * data,
			     unsigned long cu_offset,
			     unsigned long pointer_size,
			     unsigned long offset_size,
			     int dwarf_version,
			     debug_info * debug_info_p,
			     int do_loc,
			     struct dwarf_section * section)
{
  unsigned long uvalue = 0;
  unsigned char *block_start = NULL;
  unsigned char * orig_data = data;
  unsigned int bytes_read;

  switch (form)
    {
    default:
      break;

    case DW_FORM_ref_addr:
      if (dwarf_version == 2)
	{
	  uvalue = byte_get (data, pointer_size);
	  data += pointer_size;
	}
      else if (dwarf_version == 3)
	{
	  uvalue = byte_get (data, offset_size);
	  data += offset_size;
	}
      else
	{
	  error (_("Internal error: DWARF version is not 2 or 3.\n"));
	}
      break;

    case DW_FORM_addr:
      uvalue = byte_get (data, pointer_size);
      data += pointer_size;
      break;

    case DW_FORM_strp:
      uvalue = byte_get (data, offset_size);
      data += offset_size;
      break;

    case DW_FORM_ref1:
    case DW_FORM_flag:
    case DW_FORM_data1:
      uvalue = byte_get (data++, 1);
      break;

    case DW_FORM_ref2:
    case DW_FORM_data2:
      uvalue = byte_get (data, 2);
      data += 2;
      break;

    case DW_FORM_ref4:
    case DW_FORM_data4:
      uvalue = byte_get (data, 4);
      data += 4;
      break;

    case DW_FORM_sdata:
      uvalue = read_leb128 (data, & bytes_read, 1);
      data += bytes_read;
      break;

    case DW_FORM_ref_udata:
    case DW_FORM_udata:
      uvalue = read_leb128 (data, & bytes_read, 0);
      data += bytes_read;
      break;

    case DW_FORM_indirect:
      form = read_leb128 (data, & bytes_read, 0);
      data += bytes_read;
      if (!do_loc)
	printf (" %s", get_FORM_name (form));
      return read_and_display_attr_value (attribute, form, data,
					  cu_offset, pointer_size,
					  offset_size, dwarf_version,
					  debug_info_p, do_loc,
					  section);
    }

  switch (form)
    {
    case DW_FORM_ref_addr:
      if (!do_loc)
	printf (" <0x%lx>", uvalue);
      break;

    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref_udata:
      if (!do_loc)
	printf (" <0x%lx>", uvalue + cu_offset);
      break;

    case DW_FORM_data4:
    case DW_FORM_addr:
      if (!do_loc)
	printf (" 0x%lx", uvalue);
      break;

    case DW_FORM_flag:
    case DW_FORM_data1:
    case DW_FORM_data2:
    case DW_FORM_sdata:
    case DW_FORM_udata:
      if (!do_loc)
	printf (" %ld", uvalue);
      break;

    case DW_FORM_ref8:
    case DW_FORM_data8:
      if (!do_loc)
	{
	  uvalue = byte_get (data, 4);
	  printf (" 0x%lx", uvalue);
	  printf (" 0x%lx", (unsigned long) byte_get (data + 4, 4));
	}
      if ((do_loc || do_debug_loc || do_debug_ranges)
	  && num_debug_info_entries == 0)
	{
	  if (sizeof (uvalue) == 8)
	    uvalue = byte_get (data, 8);
	  else
	    error (_("DW_FORM_data8 is unsupported when sizeof (unsigned long) != 8\n"));
	}
      data += 8;
      break;

    case DW_FORM_string:
      if (!do_loc)
	printf (" %s", data);
      data += strlen ((char *) data) + 1;
      break;

    case DW_FORM_block:
      uvalue = read_leb128 (data, & bytes_read, 0);
      block_start = data + bytes_read;
      if (do_loc)
	data = block_start + uvalue;
      else
	data = display_block (block_start, uvalue);
      break;

    case DW_FORM_block1:
      uvalue = byte_get (data, 1);
      block_start = data + 1;
      if (do_loc)
	data = block_start + uvalue;
      else
	data = display_block (block_start, uvalue);
      break;

    case DW_FORM_block2:
      uvalue = byte_get (data, 2);
      block_start = data + 2;
      if (do_loc)
	data = block_start + uvalue;
      else
	data = display_block (block_start, uvalue);
      break;

    case DW_FORM_block4:
      uvalue = byte_get (data, 4);
      block_start = data + 4;
      if (do_loc)
	data = block_start + uvalue;
      else
	data = display_block (block_start, uvalue);
      break;

    case DW_FORM_strp:
      if (!do_loc)
	printf (_(" (indirect string, offset: 0x%lx): %s"),
		uvalue, fetch_indirect_string (uvalue));
      break;

    case DW_FORM_indirect:
      /* Handled above.  */
      break;

    case DW_FORM_ref_sig8:
      if (!do_loc)
	{
	  int i;
	  printf (" signature: ");
	  for (i = 0; i < 8; i++)
	    {
	      printf ("%02x", (unsigned) byte_get (data, 1));
	      data += 1;
	    }
	}
      else
        data += 8;
      break;

    default:
      warn (_("Unrecognized form: %lu\n"), form);
      break;
    }

  if ((do_loc || do_debug_loc || do_debug_ranges)
      && num_debug_info_entries == 0)
    {
      switch (attribute)
	{
	case DW_AT_frame_base:
	  have_frame_base = 1;
	case DW_AT_location:
	case DW_AT_string_length:
	case DW_AT_return_addr:
	case DW_AT_data_member_location:
	case DW_AT_vtable_elem_location:
	case DW_AT_segment:
	case DW_AT_static_link:
	case DW_AT_use_location:
    	  if (form == DW_FORM_data4 || form == DW_FORM_data8)
	    {
	      /* Process location list.  */
	      unsigned int max = debug_info_p->max_loc_offsets;
	      unsigned int num = debug_info_p->num_loc_offsets;

	      if (max == 0 || num >= max)
		{
		  max += 1024;
		  debug_info_p->loc_offsets = (long unsigned int *)
                      xcrealloc (debug_info_p->loc_offsets,
				 max, sizeof (*debug_info_p->loc_offsets));
		  debug_info_p->have_frame_base = (int *)
                      xcrealloc (debug_info_p->have_frame_base,
				 max, sizeof (*debug_info_p->have_frame_base));
		  debug_info_p->max_loc_offsets = max;
		}
	      debug_info_p->loc_offsets [num] = uvalue;
	      debug_info_p->have_frame_base [num] = have_frame_base;
	      debug_info_p->num_loc_offsets++;
	    }
	  break;

	case DW_AT_low_pc:
	  if (need_base_address)
	    debug_info_p->base_address = uvalue;
	  break;

	case DW_AT_ranges:
	  if (form == DW_FORM_data4 || form == DW_FORM_data8)
	    {
	      /* Process range list.  */
	      unsigned int max = debug_info_p->max_range_lists;
	      unsigned int num = debug_info_p->num_range_lists;

	      if (max == 0 || num >= max)
		{
		  max += 1024;
		  debug_info_p->range_lists = (long unsigned int *)
                      xcrealloc (debug_info_p->range_lists,
				 max, sizeof (*debug_info_p->range_lists));
		  debug_info_p->max_range_lists = max;
		}
	      debug_info_p->range_lists [num] = uvalue;
	      debug_info_p->num_range_lists++;
	    }
	  break;

	default:
	  break;
	}
    }

  if (do_loc)
    return data;

  /* For some attributes we can display further information.  */
  printf ("\t");

  switch (attribute)
    {
    case DW_AT_inline:
      switch (uvalue)
	{
	case DW_INL_not_inlined:
	  printf (_("(not inlined)"));
	  break;
	case DW_INL_inlined:
	  printf (_("(inlined)"));
	  break;
	case DW_INL_declared_not_inlined:
	  printf (_("(declared as inline but ignored)"));
	  break;
	case DW_INL_declared_inlined:
	  printf (_("(declared as inline and inlined)"));
	  break;
	default:
	  printf (_("  (Unknown inline attribute value: %lx)"), uvalue);
	  break;
	}
      break;

    case DW_AT_language:
      switch (uvalue)
	{
	  /* Ordered by the numeric value of these constants.  */
	case DW_LANG_C89:		printf ("(ANSI C)"); break;
	case DW_LANG_C:			printf ("(non-ANSI C)"); break;
	case DW_LANG_Ada83:		printf ("(Ada)"); break;
	case DW_LANG_C_plus_plus:	printf ("(C++)"); break;
	case DW_LANG_Cobol74:		printf ("(Cobol 74)"); break;
	case DW_LANG_Cobol85:		printf ("(Cobol 85)"); break;
	case DW_LANG_Fortran77:		printf ("(FORTRAN 77)"); break;
	case DW_LANG_Fortran90:		printf ("(Fortran 90)"); break;
	case DW_LANG_Pascal83:		printf ("(ANSI Pascal)"); break;
	case DW_LANG_Modula2:		printf ("(Modula 2)"); break;
	  /* DWARF 2.1 values.	*/
	case DW_LANG_Java:		printf ("(Java)"); break;
	case DW_LANG_C99:		printf ("(ANSI C99)"); break;
	case DW_LANG_Ada95:		printf ("(ADA 95)"); break;
	case DW_LANG_Fortran95:		printf ("(Fortran 95)"); break;
	  /* DWARF 3 values.  */
	case DW_LANG_PLI:		printf ("(PLI)"); break;
	case DW_LANG_ObjC:		printf ("(Objective C)"); break;
	case DW_LANG_ObjC_plus_plus:	printf ("(Objective C++)"); break;
	case DW_LANG_UPC:		printf ("(Unified Parallel C)"); break;
	case DW_LANG_D:			printf ("(D)"); break;
	  /* DWARF 4 values.  */
	case DW_LANG_Python:		printf ("(Python)"); break;
	  /* MIPS extension.  */
	case DW_LANG_Mips_Assembler:	printf ("(MIPS assembler)"); break;
	  /* UPC extension.  */
	case DW_LANG_Upc:		printf ("(Unified Parallel C)"); break;
	default:
	  if (uvalue >= DW_LANG_lo_user && uvalue <= DW_LANG_hi_user)
	    printf ("(implementation defined: %lx)", uvalue);
	  else
	    printf ("(Unknown: %lx)", uvalue);
	  break;
	}
      break;

    case DW_AT_encoding:
      switch (uvalue)
	{
	case DW_ATE_void:		printf ("(void)"); break;
	case DW_ATE_address:		printf ("(machine address)"); break;
	case DW_ATE_boolean:		printf ("(boolean)"); break;
	case DW_ATE_complex_float:	printf ("(complex float)"); break;
	case DW_ATE_float:		printf ("(float)"); break;
	case DW_ATE_signed:		printf ("(signed)"); break;
	case DW_ATE_signed_char:	printf ("(signed char)"); break;
	case DW_ATE_unsigned:		printf ("(unsigned)"); break;
	case DW_ATE_unsigned_char:	printf ("(unsigned char)"); break;
	  /* DWARF 2.1 values:  */
	case DW_ATE_imaginary_float:	printf ("(imaginary float)"); break;
	case DW_ATE_decimal_float:	printf ("(decimal float)"); break;
	  /* DWARF 3 values:  */
	case DW_ATE_packed_decimal:	printf ("(packed_decimal)"); break;
	case DW_ATE_numeric_string:	printf ("(numeric_string)"); break;
	case DW_ATE_edited:		printf ("(edited)"); break;
	case DW_ATE_signed_fixed:	printf ("(signed_fixed)"); break;
	case DW_ATE_unsigned_fixed:	printf ("(unsigned_fixed)"); break;
	  /* HP extensions:  */
	case DW_ATE_HP_float80:		printf ("(HP_float80)"); break;
	case DW_ATE_HP_complex_float80:	printf ("(HP_complex_float80)"); break;
	case DW_ATE_HP_float128:	printf ("(HP_float128)"); break;
	case DW_ATE_HP_complex_float128:printf ("(HP_complex_float128)"); break;
	case DW_ATE_HP_floathpintel:	printf ("(HP_floathpintel)"); break;
	case DW_ATE_HP_imaginary_float80:	printf ("(HP_imaginary_float80)"); break;
	case DW_ATE_HP_imaginary_float128:	printf ("(HP_imaginary_float128)"); break;

	default:
	  if (uvalue >= DW_ATE_lo_user
	      && uvalue <= DW_ATE_hi_user)
	    printf ("(user defined type)");
	  else
	    printf ("(unknown type)");
	  break;
	}
      break;

    case DW_AT_accessibility:
      switch (uvalue)
	{
	case DW_ACCESS_public:		printf ("(public)"); break;
	case DW_ACCESS_protected:	printf ("(protected)"); break;
	case DW_ACCESS_private:		printf ("(private)"); break;
	default:
	  printf ("(unknown accessibility)");
	  break;
	}
      break;

    case DW_AT_visibility:
      switch (uvalue)
	{
	case DW_VIS_local:		printf ("(local)"); break;
	case DW_VIS_exported:		printf ("(exported)"); break;
	case DW_VIS_qualified:		printf ("(qualified)"); break;
	default:			printf ("(unknown visibility)"); break;
	}
      break;

    case DW_AT_virtuality:
      switch (uvalue)
	{
	case DW_VIRTUALITY_none:	printf ("(none)"); break;
	case DW_VIRTUALITY_virtual:	printf ("(virtual)"); break;
	case DW_VIRTUALITY_pure_virtual:printf ("(pure_virtual)"); break;
	default:			printf ("(unknown virtuality)"); break;
	}
      break;

    case DW_AT_identifier_case:
      switch (uvalue)
	{
	case DW_ID_case_sensitive:	printf ("(case_sensitive)"); break;
	case DW_ID_up_case:		printf ("(up_case)"); break;
	case DW_ID_down_case:		printf ("(down_case)"); break;
	case DW_ID_case_insensitive:	printf ("(case_insensitive)"); break;
	default:			printf ("(unknown case)"); break;
	}
      break;

    case DW_AT_calling_convention:
      switch (uvalue)
	{
	case DW_CC_normal:	printf ("(normal)"); break;
	case DW_CC_program:	printf ("(program)"); break;
	case DW_CC_nocall:	printf ("(nocall)"); break;
	default:
	  if (uvalue >= DW_CC_lo_user
	      && uvalue <= DW_CC_hi_user)
	    printf ("(user defined)");
	  else
	    printf ("(unknown convention)");
	}
      break;

    case DW_AT_ordering:
      switch (uvalue)
	{
	case -1: printf ("(undefined)"); break;
	case 0:  printf ("(row major)"); break;
	case 1:  printf ("(column major)"); break;
	}
      break;

    case DW_AT_frame_base:
      have_frame_base = 1;
    case DW_AT_location:
    case DW_AT_string_length:
    case DW_AT_return_addr:
    case DW_AT_data_member_location:
    case DW_AT_vtable_elem_location:
    case DW_AT_segment:
    case DW_AT_static_link:
    case DW_AT_use_location:
      if (form == DW_FORM_data4 || form == DW_FORM_data8)
	printf (_("(location list)"));
      /* Fall through.  */
    case DW_AT_allocated:
    case DW_AT_associated:
    case DW_AT_data_location:
    case DW_AT_stride:
    case DW_AT_upper_bound:
    case DW_AT_lower_bound:
      if (block_start)
	{
	  int need_frame_base;

	  printf ("(");
	  need_frame_base = decode_location_expression (block_start,
							pointer_size,
							uvalue,
							cu_offset, section);
	  printf (")");
	  if (need_frame_base && !have_frame_base)
	    printf (_(" [without DW_AT_frame_base]"));
	}
      break;

    case DW_AT_import:
      {
        if (form == DW_FORM_ref_sig8)
          break;

	if (form == DW_FORM_ref1
	    || form == DW_FORM_ref2
	    || form == DW_FORM_ref4)
	  uvalue += cu_offset;

	if (uvalue >= section->size)
	  warn (_("Offset %lx used as value for DW_AT_import attribute of DIE at offset %lx is too big.\n"),
		uvalue, (unsigned long) (orig_data - section->start));
	else
	  {
	    unsigned long abbrev_number;
	    abbrev_entry * entry;

	    abbrev_number = read_leb128 (section->start + uvalue, NULL, 0);

	    printf ("[Abbrev Number: %ld", abbrev_number);
	    for (entry = first_abbrev; entry != NULL; entry = entry->next)
	      if (entry->entry == abbrev_number)
		break;
	    if (entry != NULL)
	      printf (" (%s)", get_TAG_name (entry->tag));
	    printf ("]");
	  }
      }
      break;

    default:
      break;
    }

  return data;
}

static char *
get_AT_name (unsigned long attribute)
{
  switch (attribute)
    {
    case DW_AT_sibling:			return "DW_AT_sibling";
    case DW_AT_location:		return "DW_AT_location";
    case DW_AT_name:			return "DW_AT_name";
    case DW_AT_ordering:		return "DW_AT_ordering";
    case DW_AT_subscr_data:		return "DW_AT_subscr_data";
    case DW_AT_byte_size:		return "DW_AT_byte_size";
    case DW_AT_bit_offset:		return "DW_AT_bit_offset";
    case DW_AT_bit_size:		return "DW_AT_bit_size";
    case DW_AT_element_list:		return "DW_AT_element_list";
    case DW_AT_stmt_list:		return "DW_AT_stmt_list";
    case DW_AT_low_pc:			return "DW_AT_low_pc";
    case DW_AT_high_pc:			return "DW_AT_high_pc";
    case DW_AT_language:		return "DW_AT_language";
    case DW_AT_member:			return "DW_AT_member";
    case DW_AT_discr:			return "DW_AT_discr";
    case DW_AT_discr_value:		return "DW_AT_discr_value";
    case DW_AT_visibility:		return "DW_AT_visibility";
    case DW_AT_import:			return "DW_AT_import";
    case DW_AT_string_length:		return "DW_AT_string_length";
    case DW_AT_common_reference:	return "DW_AT_common_reference";
    case DW_AT_comp_dir:		return "DW_AT_comp_dir";
    case DW_AT_const_value:		return "DW_AT_const_value";
    case DW_AT_containing_type:		return "DW_AT_containing_type";
    case DW_AT_default_value:		return "DW_AT_default_value";
    case DW_AT_inline:			return "DW_AT_inline";
    case DW_AT_is_optional:		return "DW_AT_is_optional";
    case DW_AT_lower_bound:		return "DW_AT_lower_bound";
    case DW_AT_producer:		return "DW_AT_producer";
    case DW_AT_prototyped:		return "DW_AT_prototyped";
    case DW_AT_return_addr:		return "DW_AT_return_addr";
    case DW_AT_start_scope:		return "DW_AT_start_scope";
    case DW_AT_stride_size:		return "DW_AT_stride_size";
    case DW_AT_upper_bound:		return "DW_AT_upper_bound";
    case DW_AT_abstract_origin:		return "DW_AT_abstract_origin";
    case DW_AT_accessibility:		return "DW_AT_accessibility";
    case DW_AT_address_class:		return "DW_AT_address_class";
    case DW_AT_artificial:		return "DW_AT_artificial";
    case DW_AT_base_types:		return "DW_AT_base_types";
    case DW_AT_calling_convention:	return "DW_AT_calling_convention";
    case DW_AT_count:			return "DW_AT_count";
    case DW_AT_data_member_location:	return "DW_AT_data_member_location";
    case DW_AT_decl_column:		return "DW_AT_decl_column";
    case DW_AT_decl_file:		return "DW_AT_decl_file";
    case DW_AT_decl_line:		return "DW_AT_decl_line";
    case DW_AT_declaration:		return "DW_AT_declaration";
    case DW_AT_discr_list:		return "DW_AT_discr_list";
    case DW_AT_encoding:		return "DW_AT_encoding";
    case DW_AT_external:		return "DW_AT_external";
    case DW_AT_frame_base:		return "DW_AT_frame_base";
    case DW_AT_friend:			return "DW_AT_friend";
    case DW_AT_identifier_case:		return "DW_AT_identifier_case";
    case DW_AT_macro_info:		return "DW_AT_macro_info";
    case DW_AT_namelist_items:		return "DW_AT_namelist_items";
    case DW_AT_priority:		return "DW_AT_priority";
    case DW_AT_segment:			return "DW_AT_segment";
    case DW_AT_specification:		return "DW_AT_specification";
    case DW_AT_static_link:		return "DW_AT_static_link";
    case DW_AT_type:			return "DW_AT_type";
    case DW_AT_use_location:		return "DW_AT_use_location";
    case DW_AT_variable_parameter:	return "DW_AT_variable_parameter";
    case DW_AT_virtuality:		return "DW_AT_virtuality";
    case DW_AT_vtable_elem_location:	return "DW_AT_vtable_elem_location";
      /* DWARF 2.1 values.  */
    case DW_AT_allocated:		return "DW_AT_allocated";
    case DW_AT_associated:		return "DW_AT_associated";
    case DW_AT_data_location:		return "DW_AT_data_location";
    case DW_AT_stride:			return "DW_AT_stride";
    case DW_AT_entry_pc:		return "DW_AT_entry_pc";
    case DW_AT_use_UTF8:		return "DW_AT_use_UTF8";
    case DW_AT_extension:		return "DW_AT_extension";
    case DW_AT_ranges:			return "DW_AT_ranges";
    case DW_AT_trampoline:		return "DW_AT_trampoline";
    case DW_AT_call_column:		return "DW_AT_call_column";
    case DW_AT_call_file:		return "DW_AT_call_file";
    case DW_AT_call_line:		return "DW_AT_call_line";
    case DW_AT_description:		return "DW_AT_description";
    case DW_AT_binary_scale:		return "DW_AT_binary_scale";
    case DW_AT_decimal_scale:		return "DW_AT_decimal_scale";
    case DW_AT_small:			return "DW_AT_small";
    case DW_AT_decimal_sign:		return "DW_AT_decimal_sign";
    case DW_AT_digit_count:		return "DW_AT_digit_count";
    case DW_AT_picture_string:		return "DW_AT_picture_string";
    case DW_AT_mutable:			return "DW_AT_mutable";
    case DW_AT_threads_scaled:		return "DW_AT_threads_scaled";
    case DW_AT_explicit:		return "DW_AT_explicit";
    case DW_AT_object_pointer:		return "DW_AT_object_pointer";
    case DW_AT_endianity:		return "DW_AT_endianity";
    case DW_AT_elemental:		return "DW_AT_elemental";
    case DW_AT_pure:			return "DW_AT_pure";
    case DW_AT_recursive:		return "DW_AT_recursive";
      /* DWARF 4 values.  */
    case DW_AT_signature:		return "DW_AT_signature";
    case DW_AT_main_subprogram:		return "DW_AT_main_subprogram";
    case DW_AT_data_bit_offset:		return "DW_AT_data_bit_offset";
    case DW_AT_const_expr:		return "DW_AT_const_expr";
    case DW_AT_enum_class:		return "DW_AT_enum_class";
    case DW_AT_linkage_name:		return "DW_AT_linkage_name";

      /* HP and SGI/MIPS extensions.  */
    case DW_AT_MIPS_loop_begin:			return "DW_AT_MIPS_loop_begin";
    case DW_AT_MIPS_tail_loop_begin:		return "DW_AT_MIPS_tail_loop_begin";
    case DW_AT_MIPS_epilog_begin:		return "DW_AT_MIPS_epilog_begin";
    case DW_AT_MIPS_loop_unroll_factor: 	return "DW_AT_MIPS_loop_unroll_factor";
    case DW_AT_MIPS_software_pipeline_depth: 	return "DW_AT_MIPS_software_pipeline_depth";
    case DW_AT_MIPS_linkage_name:		return "DW_AT_MIPS_linkage_name";
    case DW_AT_MIPS_stride:			return "DW_AT_MIPS_stride";
    case DW_AT_MIPS_abstract_name:		return "DW_AT_MIPS_abstract_name";
    case DW_AT_MIPS_clone_origin:		return "DW_AT_MIPS_clone_origin";
    case DW_AT_MIPS_has_inlines:		return "DW_AT_MIPS_has_inlines";

      /* HP Extensions.  */
    case DW_AT_HP_block_index:			return "DW_AT_HP_block_index";
    case DW_AT_HP_actuals_stmt_list:		return "DW_AT_HP_actuals_stmt_list";
    case DW_AT_HP_proc_per_section:		return "DW_AT_HP_proc_per_section";
    case DW_AT_HP_raw_data_ptr:			return "DW_AT_HP_raw_data_ptr";
    case DW_AT_HP_pass_by_reference:		return "DW_AT_HP_pass_by_reference";
    case DW_AT_HP_opt_level:			return "DW_AT_HP_opt_level";
    case DW_AT_HP_prof_version_id:		return "DW_AT_HP_prof_version_id";
    case DW_AT_HP_opt_flags:			return "DW_AT_HP_opt_flags";
    case DW_AT_HP_cold_region_low_pc:		return "DW_AT_HP_cold_region_low_pc";
    case DW_AT_HP_cold_region_high_pc:		return "DW_AT_HP_cold_region_high_pc";
    case DW_AT_HP_all_variables_modifiable:	return "DW_AT_HP_all_variables_modifiable";
    case DW_AT_HP_linkage_name:			return "DW_AT_HP_linkage_name";
    case DW_AT_HP_prof_flags:			return "DW_AT_HP_prof_flags";

      /* One value is shared by the MIPS and HP extensions:  */
    case DW_AT_MIPS_fde:			return "DW_AT_MIPS_fde or DW_AT_HP_unmodifiable";

      /* GNU extensions.  */
    case DW_AT_sf_names:			return "DW_AT_sf_names";
    case DW_AT_src_info:			return "DW_AT_src_info";
    case DW_AT_mac_info:			return "DW_AT_mac_info";
    case DW_AT_src_coords:			return "DW_AT_src_coords";
    case DW_AT_body_begin:			return "DW_AT_body_begin";
    case DW_AT_body_end:			return "DW_AT_body_end";
    case DW_AT_GNU_vector:			return "DW_AT_GNU_vector";
    case DW_AT_GNU_guarded_by:			return "DW_AT_GNU_guarded_by";
    case DW_AT_GNU_pt_guarded_by:		return "DW_AT_GNU_pt_guarded_by";
    case DW_AT_GNU_guarded:			return "DW_AT_GNU_guarded";
    case DW_AT_GNU_pt_guarded:			return "DW_AT_GNU_pt_guarded";
    case DW_AT_GNU_locks_excluded:		return "DW_AT_GNU_locks_excluded";
    case DW_AT_GNU_exclusive_locks_required:	return "DW_AT_GNU_exclusive_locks_required";
    case DW_AT_GNU_shared_locks_required:	return "DW_AT_GNU_shared_locks_required";
    case DW_AT_GNU_odr_signature:		return "DW_AT_GNU_odr_signature";

      /* UPC extension.  */
    case DW_AT_upc_threads_scaled:	return "DW_AT_upc_threads_scaled";

    /* PGI (STMicroelectronics) extensions.  */
    case DW_AT_PGI_lbase:		return "DW_AT_PGI_lbase";
    case DW_AT_PGI_soffset:		return "DW_AT_PGI_soffset";
    case DW_AT_PGI_lstride:		return "DW_AT_PGI_lstride";

    default:
      {
	static char buffer[100];

	snprintf (buffer, sizeof (buffer), _("Unknown AT value: %lx"),
		  attribute);
	return buffer;
      }
    }
}

static unsigned char *
read_and_display_attr (unsigned long attribute,
		       unsigned long form,
		       unsigned char * data,
		       unsigned long cu_offset,
		       unsigned long pointer_size,
		       unsigned long offset_size,
		       int dwarf_version,
		       debug_info * debug_info_p,
		       int do_loc,
		       struct dwarf_section * section)
{
  if (!do_loc)
    printf ("   %-18s:", get_AT_name (attribute));
  data = read_and_display_attr_value (attribute, form, data, cu_offset,
				      pointer_size, offset_size,
				      dwarf_version, debug_info_p,
				      do_loc, section);
  if (!do_loc)
    printf ("\n");
  return data;
}


/* Process the contents of a .debug_info section.  If do_loc is non-zero
   then we are scanning for location lists and we do not want to display
   anything to the user.  If do_types is non-zero, we are processing
   a .debug_types section instead of a .debug_info section.  */

static int
process_debug_info (struct dwarf_section *section,
		    void *file,
		    int do_loc,
		    int do_types)
{
  unsigned char *start = section->start;
  unsigned char *end = start + section->size;
  unsigned char *section_begin;
  unsigned int unit;
  unsigned int num_units = 0;

  if ((do_loc || do_debug_loc || do_debug_ranges)
      && num_debug_info_entries == 0
      && ! do_types)
    {
      unsigned long length;

      /* First scan the section to get the number of comp units.  */
      for (section_begin = start, num_units = 0; section_begin < end;
	   num_units ++)
	{
	  /* Read the first 4 bytes.  For a 32-bit DWARF section, this
	     will be the length.  For a 64-bit DWARF section, it'll be
	     the escape code 0xffffffff followed by an 8 byte length.  */
	  length = byte_get (section_begin, 4);

	  if (length == 0xffffffff)
	    {
	      length = byte_get (section_begin + 4, 8);
	      section_begin += length + 12;
	    }
	  else if (length >= 0xfffffff0 && length < 0xffffffff)
	    {
	      warn (_("Reserved length value (%lx) found in section %s\n"), length, section->name);
	      return 0;
	    }
	  else
	    section_begin += length + 4;

	  /* Negative values are illegal, they may even cause infinite
	     looping.  This can happen if we can't accurately apply
	     relocations to an object file.  */
	  if ((signed long) length <= 0)
	    {
	      warn (_("Corrupt unit length (%lx) found in section %s\n"), length, section->name);
	      return 0;
	    }
	}

      if (num_units == 0)
	{
	  error (_("No comp units in %s section ?"), section->name);
	  return 0;
	}

      /* Then allocate an array to hold the information.  */
      debug_information = (debug_info *) cmalloc (num_units,
                                                  sizeof (* debug_information));
      if (debug_information == NULL)
	{
	  error (_("Not enough memory for a debug info array of %u entries"),
		 num_units);
	  return 0;
	}
    }

  if (!do_loc)
    {
      printf (_("Contents of the %s section:\n\n"), section->name);

      load_debug_section (str, file);
    }

  load_debug_section (abbrev, file);
  if (debug_displays [abbrev].section.start == NULL)
    {
      warn (_("Unable to locate %s section!\n"),
	    debug_displays [abbrev].section.name);
      return 0;
    }

  for (section_begin = start, unit = 0; start < end; unit++)
    {
      DWARF2_Internal_CompUnit compunit;
      unsigned char *hdrptr;
      unsigned char *cu_abbrev_offset_ptr;
      unsigned char *tags;
      int level;
      unsigned long cu_offset;
      int offset_size;
      int initial_length_size;
      unsigned char signature[8];
      unsigned long type_offset = 0;

      hdrptr = start;

      compunit.cu_length = byte_get (hdrptr, 4);
      hdrptr += 4;

      if (compunit.cu_length == 0xffffffff)
	{
	  compunit.cu_length = byte_get (hdrptr, 8);
	  hdrptr += 8;
	  offset_size = 8;
	  initial_length_size = 12;
	}
      else
	{
	  offset_size = 4;
	  initial_length_size = 4;
	}

      compunit.cu_version = byte_get (hdrptr, 2);
      hdrptr += 2;

      cu_offset = start - section_begin;

      cu_abbrev_offset_ptr = hdrptr;
      compunit.cu_abbrev_offset = byte_get (hdrptr, offset_size);
      hdrptr += offset_size;

      compunit.cu_pointer_size = byte_get (hdrptr, 1);
      hdrptr += 1;

      if (do_types)
        {
          int i;

          for (i = 0; i < 8; i++)
            {
              signature[i] = byte_get (hdrptr, 1);
              hdrptr += 1;
            }

          type_offset = byte_get (hdrptr, offset_size);
          hdrptr += offset_size;
        }

      if ((do_loc || do_debug_loc || do_debug_ranges)
	  && num_debug_info_entries == 0
	  && ! do_types)
	{
	  debug_information [unit].cu_offset = cu_offset;
	  debug_information [unit].pointer_size
	    = compunit.cu_pointer_size;
	  debug_information [unit].base_address = 0;
	  debug_information [unit].loc_offsets = NULL;
	  debug_information [unit].have_frame_base = NULL;
	  debug_information [unit].max_loc_offsets = 0;
	  debug_information [unit].num_loc_offsets = 0;
	  debug_information [unit].range_lists = NULL;
	  debug_information [unit].max_range_lists= 0;
	  debug_information [unit].num_range_lists = 0;
	}

      if (!do_loc)
	{
	  printf (_("  Compilation Unit @ offset 0x%lx:\n"), cu_offset);
	  printf (_("   Length:        0x%lx (%s)\n"), compunit.cu_length,
		  initial_length_size == 8 ? "64-bit" : "32-bit");
	  printf (_("   Version:       %d\n"), compunit.cu_version);
	  printf (_("   Abbrev Offset: %ld\n"), compunit.cu_abbrev_offset);
	  printf (_("   Pointer Size:  %d\n"), compunit.cu_pointer_size);
	  if (do_types)
	    {
	      int i;
	      printf (_("   Signature:     "));
	      for (i = 0; i < 8; i++)
	        printf ("%02x", signature[i]);
	      printf ("\n");
	      printf (_("   Type Offset:   0x%lx\n"), type_offset);
	    }
	}

      if (cu_offset + compunit.cu_length + initial_length_size
	  > section->size)
	{
	  warn (_("Debug info is corrupted, length of CU at %lx extends beyond end of section (length = %lx)\n"),
		cu_offset, compunit.cu_length);
	  break;
	}
      tags = hdrptr;
      start += compunit.cu_length + initial_length_size;

      if (compunit.cu_version != 2 && compunit.cu_version != 3)
	{
	  warn (_("CU at offset %lx contains corrupt or unsupported version number: %d.\n"),
		cu_offset, compunit.cu_version);
	  continue;
	}

      free_abbrevs ();

      /* Process the abbrevs used by this compilation unit. DWARF
	 sections under Mach-O have non-zero addresses.  */
      if (compunit.cu_abbrev_offset >= debug_displays [abbrev].section.size)
	warn (_("Debug info is corrupted, abbrev offset (%lx) is larger than abbrev section size (%lx)\n"),
	      (unsigned long) compunit.cu_abbrev_offset,
	      (unsigned long) debug_displays [abbrev].section.size);
      else
	process_abbrev_section
	  ((unsigned char *) debug_displays [abbrev].section.start
	   + compunit.cu_abbrev_offset - debug_displays [abbrev].section.address,
	   (unsigned char *) debug_displays [abbrev].section.start
	   + debug_displays [abbrev].section.size);

      level = 0;
      while (tags < start)
	{
	  unsigned int bytes_read;
	  unsigned long abbrev_number;
	  unsigned long die_offset;
	  abbrev_entry *entry;
	  abbrev_attr *attr;

	  die_offset = tags - section_begin;

	  abbrev_number = read_leb128 (tags, & bytes_read, 0);
	  tags += bytes_read;

	  /* A null DIE marks the end of a list of siblings or it may also be
	     a section padding.  */
	  if (abbrev_number == 0)
	    {
	      /* Check if it can be a section padding for the last CU.  */
	      if (level == 0 && start == end)
		{
		  unsigned char *chk;

		  for (chk = tags; chk < start; chk++)
		    if (*chk != 0)
		      break;
		  if (chk == start)
		    break;
		}

	      --level;
	      if (level < 0)
		{
		  static unsigned num_bogus_warns = 0;

		  if (num_bogus_warns < 3)
		    {
		      warn (_("Bogus end-of-siblings marker detected at offset %lx in .debug_info section\n"),
			    die_offset);
		      num_bogus_warns ++;
		      if (num_bogus_warns == 3)
			warn (_("Further warnings about bogus end-of-sibling markers suppressed\n"));
		    }
		}
	      continue;
	    }

	  if (!do_loc)
	    printf (_(" <%d><%lx>: Abbrev Number: %lu"),
		    level, die_offset, abbrev_number);

	  /* Scan through the abbreviation list until we reach the
	     correct entry.  */
	  for (entry = first_abbrev;
	       entry && entry->entry != abbrev_number;
	       entry = entry->next)
	    continue;

	  if (entry == NULL)
	    {
	      if (!do_loc)
		{
		  printf ("\n");
		  fflush (stdout);
		}
	      warn (_("DIE at offset %lx refers to abbreviation number %lu which does not exist\n"),
		    die_offset, abbrev_number);
	      return 0;
	    }

	  if (!do_loc)
	    printf (_(" (%s)\n"), get_TAG_name (entry->tag));

	  switch (entry->tag)
	    {
	    default:
	      need_base_address = 0;
	      break;
	    case DW_TAG_compile_unit:
	      need_base_address = 1;
	      break;
	    case DW_TAG_entry_point:
	    case DW_TAG_subprogram:
	      need_base_address = 0;
	      /* Assuming that there is no DW_AT_frame_base.  */
	      have_frame_base = 0;
	      break;
	    }

	  for (attr = entry->first_attr; attr; attr = attr->next)
	    {
	      if (! do_loc)
		/* Show the offset from where the tag was extracted.  */
		printf ("    <%2lx>", (unsigned long)(tags - section_begin));

	      tags = read_and_display_attr (attr->attribute,
					    attr->form,
					    tags, cu_offset,
					    compunit.cu_pointer_size,
					    offset_size,
					    compunit.cu_version,
					    debug_information + unit,
					    do_loc, section);
	    }

 	  if (entry->children)
 	    ++level;
 	}
    }

  /* Set num_debug_info_entries here so that it can be used to check if
     we need to process .debug_loc and .debug_ranges sections.  */
  if ((do_loc || do_debug_loc || do_debug_ranges)
      && num_debug_info_entries == 0
      && ! do_types)
    num_debug_info_entries = num_units;

  if (!do_loc)
    {
      printf ("\n");
    }

  return 1;
}

/* Locate and scan the .debug_info section in the file and record the pointer
   sizes and offsets for the compilation units in it.  Usually an executable
   will have just one pointer size, but this is not guaranteed, and so we try
   not to make any assumptions.  Returns zero upon failure, or the number of
   compilation units upon success.  */

static unsigned int
load_debug_info (void * file)
{
  /* Reset the last pointer size so that we can issue correct error
     messages if we are displaying the contents of more than one section.  */
  last_pointer_size = 0;
  warned_about_missing_comp_units = FALSE;

  /* If we have already tried and failed to load the .debug_info
     section then do not bother to repear the task.  */
  if (num_debug_info_entries == DEBUG_INFO_UNAVAILABLE)
    return 0;

  /* If we already have the information there is nothing else to do.  */
  if (num_debug_info_entries > 0)
    return num_debug_info_entries;

  if (load_debug_section (info, file)
      && process_debug_info (&debug_displays [info].section, file, 1, 0))
    return num_debug_info_entries;

  num_debug_info_entries = DEBUG_INFO_UNAVAILABLE;
  return 0;
}

static int
display_debug_lines_raw (struct dwarf_section *section,
			 unsigned char *data,
                         unsigned char *end)
{
  unsigned char *start = section->start;

  printf (_("Raw dump of debug contents of section %s:\n\n"),
          section->name);

  while (data < end)
    {
      DWARF2_Internal_LineInfo info;
      unsigned char *standard_opcodes;
      unsigned char *end_of_sequence;
      unsigned char *hdrptr;
      unsigned long hdroff;
      int initial_length_size;
      int offset_size;
      int i;

      hdrptr = data;
      hdroff = hdrptr - start;

      /* Check the length of the block.  */
      info.li_length = byte_get (hdrptr, 4);
      hdrptr += 4;

      if (info.li_length == 0xffffffff)
	{
	  /* This section is 64-bit DWARF 3.  */
	  info.li_length = byte_get (hdrptr, 8);
	  hdrptr += 8;
	  offset_size = 8;
	  initial_length_size = 12;
	}
      else
	{
	  offset_size = 4;
	  initial_length_size = 4;
	}

      if (info.li_length + initial_length_size > section->size)
	{
	  warn
	    (_("The information in section %s appears to be corrupt - the section is too small\n"),
	     section->name);
	  return 0;
	}

      /* Check its version number.  */
      info.li_version = byte_get (hdrptr, 2);
      hdrptr += 2;
      if (info.li_version != 2 && info.li_version != 3)
	{
	  warn (_("Only DWARF version 2 and 3 line info is currently supported.\n"));
	  return 0;
	}

      info.li_prologue_length = byte_get (hdrptr, offset_size);
      hdrptr += offset_size;
      info.li_min_insn_length = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_default_is_stmt = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_line_base = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_line_range = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_opcode_base = byte_get (hdrptr, 1);
      hdrptr++;

      /* Sign extend the line base field.  */
      info.li_line_base <<= 24;
      info.li_line_base >>= 24;

      printf (_("  Offset:                      0x%lx\n"), hdroff);
      printf (_("  Length:                      %ld\n"), info.li_length);
      printf (_("  DWARF Version:               %d\n"), info.li_version);
      printf (_("  Prologue Length:             %d\n"), info.li_prologue_length);
      printf (_("  Minimum Instruction Length:  %d\n"), info.li_min_insn_length);
      printf (_("  Initial value of 'is_stmt':  %d\n"), info.li_default_is_stmt);
      printf (_("  Line Base:                   %d\n"), info.li_line_base);
      printf (_("  Line Range:                  %d\n"), info.li_line_range);
      printf (_("  Opcode Base:                 %d\n"), info.li_opcode_base);

      end_of_sequence = data + info.li_length + initial_length_size;

      reset_state_machine (info.li_default_is_stmt);

      /* Display the contents of the Opcodes table.  */
      standard_opcodes = hdrptr;

      printf (_("\n Opcodes:\n"));

      for (i = 1; i < info.li_opcode_base; i++)
	printf (_("  Opcode %d has %d args\n"), i, standard_opcodes[i - 1]);

      /* Display the contents of the Directory table.  */
      data = standard_opcodes + info.li_opcode_base - 1;

      if (*data == 0)
	printf (_("\n The Directory Table is empty.\n"));
      else
	{
	  printf (_("\n The Directory Table:\n"));

	  while (*data != 0)
	    {
	      printf (_("  %s\n"), data);

	      data += strlen ((char *) data) + 1;
	    }
	}

      /* Skip the NUL at the end of the table.  */
      data++;

      /* Display the contents of the File Name table.  */
      if (*data == 0)
	printf (_("\n The File Name Table is empty.\n"));
      else
	{
	  printf (_("\n The File Name Table:\n"));
	  printf (_("  Entry\tDir\tTime\tSize\tName\n"));

	  while (*data != 0)
	    {
	      unsigned char *name;
	      unsigned int bytes_read;

	      printf (_("  %d\t"), ++state_machine_regs.last_file_entry);
	      name = data;

	      data += strlen ((char *) data) + 1;

	      printf (_("%lu\t"), read_leb128 (data, & bytes_read, 0));
	      data += bytes_read;
	      printf (_("%lu\t"), read_leb128 (data, & bytes_read, 0));
	      data += bytes_read;
	      printf (_("%lu\t"), read_leb128 (data, & bytes_read, 0));
	      data += bytes_read;
	      printf (_("%s\n"), name);
	    }
	}

      /* Skip the NUL at the end of the table.  */
      data++;

      /* Now display the statements.  */
      printf (_("\n Line Number Statements:\n"));

      while (data < end_of_sequence)
	{
	  unsigned char op_code;
	  int adv;
	  unsigned long int uladv;
	  unsigned int bytes_read;

	  op_code = *data++;

	  if (op_code >= info.li_opcode_base)
	    {
	      op_code -= info.li_opcode_base;
	      uladv = (op_code / info.li_line_range) * info.li_min_insn_length;
	      state_machine_regs.address += uladv;
	      printf (_("  Special opcode %d: advance Address by %lu to 0x%lx"),
		      op_code, uladv, state_machine_regs.address);
	      adv = (op_code % info.li_line_range) + info.li_line_base;
	      state_machine_regs.line += adv;
	      printf (_(" and Line by %d to %d\n"),
		      adv, state_machine_regs.line);
	    }
	  else switch (op_code)
	    {
	    case DW_LNS_extended_op:
	      data += process_extended_line_op (data, info.li_default_is_stmt);
	      break;

	    case DW_LNS_copy:
	      printf (_("  Copy\n"));
	      break;

	    case DW_LNS_advance_pc:
	      uladv = read_leb128 (data, & bytes_read, 0);
	      uladv *= info.li_min_insn_length;
	      data += bytes_read;
	      state_machine_regs.address += uladv;
	      printf (_("  Advance PC by %lu to 0x%lx\n"), uladv,
		      state_machine_regs.address);
	      break;

	    case DW_LNS_advance_line:
	      adv = read_leb128 (data, & bytes_read, 1);
	      data += bytes_read;
	      state_machine_regs.line += adv;
	      printf (_("  Advance Line by %d to %d\n"), adv,
		      state_machine_regs.line);
	      break;

	    case DW_LNS_set_file:
	      adv = read_leb128 (data, & bytes_read, 0);
	      data += bytes_read;
	      printf (_("  Set File Name to entry %d in the File Name Table\n"),
		      adv);
	      state_machine_regs.file = adv;
	      break;

	    case DW_LNS_set_column:
	      uladv = read_leb128 (data, & bytes_read, 0);
	      data += bytes_read;
	      printf (_("  Set column to %lu\n"), uladv);
	      state_machine_regs.column = uladv;
	      break;

	    case DW_LNS_negate_stmt:
	      adv = state_machine_regs.is_stmt;
	      adv = ! adv;
	      printf (_("  Set is_stmt to %d\n"), adv);
	      state_machine_regs.is_stmt = adv;
	      break;

	    case DW_LNS_set_basic_block:
	      printf (_("  Set basic block\n"));
	      state_machine_regs.basic_block = 1;
	      break;

	    case DW_LNS_const_add_pc:
	      uladv = (((255 - info.li_opcode_base) / info.li_line_range)
		      * info.li_min_insn_length);
	      state_machine_regs.address += uladv;
	      printf (_("  Advance PC by constant %lu to 0x%lx\n"), uladv,
		      state_machine_regs.address);
	      break;

	    case DW_LNS_fixed_advance_pc:
	      uladv = byte_get (data, 2);
	      data += 2;
	      state_machine_regs.address += uladv;
	      printf (_("  Advance PC by fixed size amount %lu to 0x%lx\n"),
		      uladv, state_machine_regs.address);
	      break;

	    case DW_LNS_set_prologue_end:
	      printf (_("  Set prologue_end to true\n"));
	      break;

	    case DW_LNS_set_epilogue_begin:
	      printf (_("  Set epilogue_begin to true\n"));
	      break;

	    case DW_LNS_set_isa:
	      uladv = read_leb128 (data, & bytes_read, 0);
	      data += bytes_read;
	      printf (_("  Set ISA to %lu\n"), uladv);
	      break;

	    default:
	      printf (_("  Unknown opcode %d with operands: "), op_code);

	      for (i = standard_opcodes[op_code - 1]; i > 0 ; --i)
		{
		  printf ("0x%lx%s", read_leb128 (data, &bytes_read, 0),
			  i == 1 ? "" : ", ");
		  data += bytes_read;
		}
	      putchar ('\n');
	      break;
	    }
	}
      putchar ('\n');
    }

  return 1;
}

typedef struct
{
    unsigned char *name;
    unsigned int directory_index;
    unsigned int modification_date;
    unsigned int length;
} File_Entry;

/* Output a decoded representation of the .debug_line section.  */

static int
display_debug_lines_decoded (struct dwarf_section *section,
			     unsigned char *data,
			     unsigned char *end)
{
  printf (_("Decoded dump of debug contents of section %s:\n\n"),
          section->name);

  while (data < end)
    {
      /* This loop amounts to one iteration per compilation unit.  */
      DWARF2_Internal_LineInfo info;
      unsigned char *standard_opcodes;
      unsigned char *end_of_sequence;
      unsigned char *hdrptr;
      int initial_length_size;
      int offset_size;
      int i;
      File_Entry *file_table = NULL;
      unsigned char **directory_table = NULL;
      unsigned int prev_line = 0;

      hdrptr = data;

      /* Extract information from the Line Number Program Header.
        (section 6.2.4 in the Dwarf3 doc).  */

      /* Get the length of this CU's line number information block.  */
      info.li_length = byte_get (hdrptr, 4);
      hdrptr += 4;

      if (info.li_length == 0xffffffff)
        {
          /* This section is 64-bit DWARF 3.  */
          info.li_length = byte_get (hdrptr, 8);
          hdrptr += 8;
          offset_size = 8;
          initial_length_size = 12;
        }
      else
        {
          offset_size = 4;
          initial_length_size = 4;
        }

      if (info.li_length + initial_length_size > section->size)
        {
          warn (_("The line info appears to be corrupt - "
                  "the section is too small\n"));
          return 0;
        }

      /* Get this CU's Line Number Block version number.  */
      info.li_version = byte_get (hdrptr, 2);
      hdrptr += 2;
      if (info.li_version != 2 && info.li_version != 3)
        {
          warn (_("Only DWARF version 2 and 3 line info is currently "
                "supported.\n"));
          return 0;
        }

      info.li_prologue_length = byte_get (hdrptr, offset_size);
      hdrptr += offset_size;
      info.li_min_insn_length = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_default_is_stmt = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_line_base = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_line_range = byte_get (hdrptr, 1);
      hdrptr++;
      info.li_opcode_base = byte_get (hdrptr, 1);
      hdrptr++;

      /* Sign extend the line base field.  */
      info.li_line_base <<= 24;
      info.li_line_base >>= 24;

      /* Find the end of this CU's Line Number Information Block.  */
      end_of_sequence = data + info.li_length + initial_length_size;

      reset_state_machine (info.li_default_is_stmt);

      /* Save a pointer to the contents of the Opcodes table.  */
      standard_opcodes = hdrptr;

      /* Traverse the Directory table just to count entries.  */
      data = standard_opcodes + info.li_opcode_base - 1;
      if (*data != 0)
        {
          unsigned int n_directories = 0;
          unsigned char *ptr_directory_table = data;
          int i;

	  while (*data != 0)
	    {
	      data += strlen ((char *) data) + 1;
	      n_directories++;
	    }

          /* Go through the directory table again to save the directories.  */
          directory_table = (unsigned char **)
              xmalloc (n_directories * sizeof (unsigned char *));

          i = 0;
          while (*ptr_directory_table != 0)
            {
              directory_table[i] = ptr_directory_table;
              ptr_directory_table += strlen ((char *) ptr_directory_table) + 1;
              i++;
            }
        }
      /* Skip the NUL at the end of the table.  */
      data++;

      /* Traverse the File Name table just to count the entries.  */
      if (*data != 0)
        {
          unsigned int n_files = 0;
          unsigned char *ptr_file_name_table = data;
          int i;

          while (*data != 0)
            {
	      unsigned int bytes_read;

              /* Skip Name, directory index, last modification time and length
                 of file.  */
              data += strlen ((char *) data) + 1;
              read_leb128 (data, & bytes_read, 0);
              data += bytes_read;
              read_leb128 (data, & bytes_read, 0);
              data += bytes_read;
              read_leb128 (data, & bytes_read, 0);
              data += bytes_read;

              n_files++;
            }

          /* Go through the file table again to save the strings.  */
          file_table = (File_Entry *) xmalloc (n_files * sizeof (File_Entry));

          i = 0;
          while (*ptr_file_name_table != 0)
            {
              unsigned int bytes_read;

              file_table[i].name = ptr_file_name_table;
              ptr_file_name_table += strlen ((char *) ptr_file_name_table) + 1;

              /* We are not interested in directory, time or size.  */
              file_table[i].directory_index = read_leb128 (ptr_file_name_table,
                                                           & bytes_read, 0);
              ptr_file_name_table += bytes_read;
              file_table[i].modification_date = read_leb128 (ptr_file_name_table,
							     & bytes_read, 0);
              ptr_file_name_table += bytes_read;
              file_table[i].length = read_leb128 (ptr_file_name_table, & bytes_read, 0);
              ptr_file_name_table += bytes_read;
              i++;
            }
          i = 0;

          /* Print the Compilation Unit's name and a header.  */
          if (directory_table == NULL)
            {
              printf (_("CU: %s:\n"), file_table[0].name);
              printf (_("File name                            Line number    Starting address\n"));
            }
          else
            {
              if (do_wide || strlen ((char *) directory_table[0]) < 76)
                {
                  printf (_("CU: %s/%s:\n"), directory_table[0],
                          file_table[0].name);
                }
              else
                {
                  printf (_("%s:\n"), file_table[0].name);
                }
              printf (_("File name                            Line number    Starting address\n"));
            }
        }

      /* Skip the NUL at the end of the table.  */
      data++;

      /* This loop iterates through the Dwarf Line Number Program.  */
      while (data < end_of_sequence)
        {
	  unsigned char op_code;
          int adv;
          unsigned long int uladv;
          unsigned int bytes_read;
          int is_special_opcode = 0;

          op_code = *data++;
          prev_line = state_machine_regs.line;

          if (op_code >= info.li_opcode_base)
	    {
	      op_code -= info.li_opcode_base;
              uladv = (op_code / info.li_line_range) * info.li_min_insn_length;
              state_machine_regs.address += uladv;

              adv = (op_code % info.li_line_range) + info.li_line_base;
              state_machine_regs.line += adv;
              is_special_opcode = 1;
            }
          else switch (op_code)
            {
            case DW_LNS_extended_op:
              {
                unsigned int ext_op_code_len;
                unsigned int bytes_read;
                unsigned char ext_op_code;
                unsigned char *op_code_data = data;

                ext_op_code_len = read_leb128 (op_code_data, &bytes_read, 0);
                op_code_data += bytes_read;

                if (ext_op_code_len == 0)
                  {
                    warn (_("badly formed extended line op encountered!\n"));
                    break;
                  }
                ext_op_code_len += bytes_read;
                ext_op_code = *op_code_data++;

                switch (ext_op_code)
                  {
                  case DW_LNE_end_sequence:
                    reset_state_machine (info.li_default_is_stmt);
                    break;
                  case DW_LNE_set_address:
                    state_machine_regs.address =
                    byte_get (op_code_data, ext_op_code_len - bytes_read - 1);
                    break;
                  case DW_LNE_define_file:
                    {
                      unsigned int dir_index = 0;

                      ++state_machine_regs.last_file_entry;
                      op_code_data += strlen ((char *) op_code_data) + 1;
                      dir_index = read_leb128 (op_code_data, & bytes_read, 0);
                      op_code_data += bytes_read;
                      read_leb128 (op_code_data, & bytes_read, 0);
                      op_code_data += bytes_read;
                      read_leb128 (op_code_data, & bytes_read, 0);

                      printf (_("%s:\n"), directory_table[dir_index]);
                      break;
                    }
                  default:
                    printf (_("UNKNOWN: length %d\n"), ext_op_code_len - bytes_read);
                    break;
                  }
                data += ext_op_code_len;
                break;
              }
            case DW_LNS_copy:
              break;

            case DW_LNS_advance_pc:
              uladv = read_leb128 (data, & bytes_read, 0);
              uladv *= info.li_min_insn_length;
              data += bytes_read;
              state_machine_regs.address += uladv;
              break;

            case DW_LNS_advance_line:
              adv = read_leb128 (data, & bytes_read, 1);
              data += bytes_read;
              state_machine_regs.line += adv;
              break;

            case DW_LNS_set_file:
              adv = read_leb128 (data, & bytes_read, 0);
              data += bytes_read;
              state_machine_regs.file = adv;
              if (file_table[state_machine_regs.file - 1].directory_index == 0)
                {
                  /* If directory index is 0, that means current directory.  */
                  printf (_("\n./%s:[++]\n"),
                          file_table[state_machine_regs.file - 1].name);
                }
              else
                {
                  /* The directory index starts counting at 1.  */
                  printf (_("\n%s/%s:\n"),
                          directory_table[file_table[state_machine_regs.file - 1].directory_index - 1],
                          file_table[state_machine_regs.file - 1].name);
                }
              break;

            case DW_LNS_set_column:
              uladv = read_leb128 (data, & bytes_read, 0);
              data += bytes_read;
              state_machine_regs.column = uladv;
              break;

            case DW_LNS_negate_stmt:
              adv = state_machine_regs.is_stmt;
              adv = ! adv;
              state_machine_regs.is_stmt = adv;
              break;

            case DW_LNS_set_basic_block:
              state_machine_regs.basic_block = 1;
              break;

            case DW_LNS_const_add_pc:
              uladv = (((255 - info.li_opcode_base) / info.li_line_range)
                       * info.li_min_insn_length);
              state_machine_regs.address += uladv;
              break;

            case DW_LNS_fixed_advance_pc:
              uladv = byte_get (data, 2);
              data += 2;
              state_machine_regs.address += uladv;
              break;

            case DW_LNS_set_prologue_end:
              break;

            case DW_LNS_set_epilogue_begin:
              break;

            case DW_LNS_set_isa:
              uladv = read_leb128 (data, & bytes_read, 0);
              data += bytes_read;
              printf (_("  Set ISA to %lu\n"), uladv);
              break;

            default:
              printf (_("  Unknown opcode %d with operands: "), op_code);

              for (i = standard_opcodes[op_code - 1]; i > 0 ; --i)
                {
                  printf ("0x%lx%s", read_leb128 (data, &bytes_read, 0),
                          i == 1 ? "" : ", ");
                  data += bytes_read;
                }
              putchar ('\n');
              break;
            }

          /* Only Special opcodes, DW_LNS_copy and DW_LNE_end_sequence adds a row
             to the DWARF address/line matrix.  */
          if ((is_special_opcode) || (op_code == DW_LNE_end_sequence)
	      || (op_code == DW_LNS_copy))
            {
              const unsigned int MAX_FILENAME_LENGTH = 35;
              char *fileName = (char *)file_table[state_machine_regs.file - 1].name;
              char *newFileName = NULL;
              size_t fileNameLength = strlen (fileName);

              if ((fileNameLength > MAX_FILENAME_LENGTH) && (!do_wide))
                {
                  newFileName = (char *) xmalloc (MAX_FILENAME_LENGTH + 1);
                  /* Truncate file name */
                  strncpy (newFileName,
                           fileName + fileNameLength - MAX_FILENAME_LENGTH,
                           MAX_FILENAME_LENGTH + 1);
                }
              else
                {
                  newFileName = (char *) xmalloc (fileNameLength + 1);
                  strncpy (newFileName, fileName, fileNameLength + 1);
                }

              if (!do_wide || (fileNameLength <= MAX_FILENAME_LENGTH))
                {
                  printf (_("%-35s  %11d  %#18lx\n"), newFileName,
                          state_machine_regs.line, state_machine_regs.address);
                }
              else
                {
                  printf (_("%s  %11d  %#18lx\n"), newFileName,
                          state_machine_regs.line, state_machine_regs.address);
                }

              if (op_code == DW_LNE_end_sequence)
		printf ("\n");

              free (newFileName);
            }
        }
      free (file_table);
      file_table = NULL;
      free (directory_table);
      directory_table = NULL;
      putchar ('\n');
    }

  return 1;
}

static int
display_debug_lines (struct dwarf_section *section, void *file)
{
  unsigned char *data = section->start;
  unsigned char *end = data + section->size;
  int retValRaw = 1;
  int retValDecoded = 1;

  if (load_debug_info (file) == 0)
    {
      warn (_("Unable to load/parse the .debug_info section, so cannot interpret the %s section.\n"),
            section->name);
      return 0;
    }

  if (do_debug_lines == 0)
    do_debug_lines |= FLAG_DEBUG_LINES_RAW;

  if (do_debug_lines & FLAG_DEBUG_LINES_RAW)
    retValRaw = display_debug_lines_raw (section, data, end);

  if (do_debug_lines & FLAG_DEBUG_LINES_DECODED)
    retValDecoded = display_debug_lines_decoded (section, data, end);

  if (!retValRaw || !retValDecoded)
    return 0;

  return 1;
}

static debug_info *
find_debug_info_for_offset (unsigned long offset)
{
  unsigned int i;

  if (num_debug_info_entries == DEBUG_INFO_UNAVAILABLE)
    return NULL;

  for (i = 0; i < num_debug_info_entries; i++)
    if (debug_information[i].cu_offset == offset)
      return debug_information + i;

  return NULL;
}

static int
display_debug_pubnames (struct dwarf_section *section,
			void *file ATTRIBUTE_UNUSED)
{
  DWARF2_Internal_PubNames pubnames;
  unsigned char *start = section->start;
  unsigned char *end = start + section->size;

  /* It does not matter if this load fails,
     we test for that later on.  */
  load_debug_info (file);

  printf (_("Contents of the %s section:\n\n"), section->name);

  while (start < end)
    {
      unsigned char *data;
      unsigned long offset;
      int offset_size, initial_length_size;

      data = start;

      pubnames.pn_length = byte_get (data, 4);
      data += 4;
      if (pubnames.pn_length == 0xffffffff)
	{
	  pubnames.pn_length = byte_get (data, 8);
	  data += 8;
	  offset_size = 8;
	  initial_length_size = 12;
	}
      else
	{
	  offset_size = 4;
	  initial_length_size = 4;
	}

      pubnames.pn_version = byte_get (data, 2);
      data += 2;

      pubnames.pn_offset = byte_get (data, offset_size);
      data += offset_size;

      if (num_debug_info_entries != DEBUG_INFO_UNAVAILABLE
	  && num_debug_info_entries > 0
	  && find_debug_info_for_offset (pubnames.pn_offset) == NULL)
	warn (_(".debug_info offset of 0x%lx in %s section does not point to a CU header.\n"),
	      pubnames.pn_offset, section->name);

      pubnames.pn_size = byte_get (data, offset_size);
      data += offset_size;

      start += pubnames.pn_length + initial_length_size;

      if (pubnames.pn_version != 2 && pubnames.pn_version != 3)
	{
	  static int warned = 0;

	  if (! warned)
	    {
	      warn (_("Only DWARF 2 and 3 pubnames are currently supported\n"));
	      warned = 1;
	    }

	  continue;
	}

      printf (_("  Length:                              %ld\n"),
	      pubnames.pn_length);
      printf (_("  Version:                             %d\n"),
	      pubnames.pn_version);
      printf (_("  Offset into .debug_info section:     0x%lx\n"),
	      pubnames.pn_offset);
      printf (_("  Size of area in .debug_info section: %ld\n"),
	      pubnames.pn_size);

      printf (_("\n    Offset\tName\n"));

      do
	{
	  offset = byte_get (data, offset_size);

	  if (offset != 0)
	    {
	      data += offset_size;
	      printf ("    %-6lx\t%s\n", offset, data);
	      data += strlen ((char *) data) + 1;
	    }
	}
      while (offset != 0);
    }

  printf ("\n");
  return 1;
}

static int
display_debug_macinfo (struct dwarf_section *section,
		       void *file ATTRIBUTE_UNUSED)
{
  unsigned char *start = section->start;
  unsigned char *end = start + section->size;
  unsigned char *curr = start;
  unsigned int bytes_read;
  enum dwarf_macinfo_record_type op;

  printf (_("Contents of the %s section:\n\n"), section->name);

  while (curr < end)
    {
      unsigned int lineno;
      const char *string;

      op = (enum dwarf_macinfo_record_type) *curr;
      curr++;

      switch (op)
	{
	case DW_MACINFO_start_file:
	  {
	    unsigned int filenum;

	    lineno = read_leb128 (curr, & bytes_read, 0);
	    curr += bytes_read;
	    filenum = read_leb128 (curr, & bytes_read, 0);
	    curr += bytes_read;

	    printf (_(" DW_MACINFO_start_file - lineno: %d filenum: %d\n"),
		    lineno, filenum);
	  }
	  break;

	case DW_MACINFO_end_file:
	  printf (_(" DW_MACINFO_end_file\n"));
	  break;

	case DW_MACINFO_define:
	  lineno = read_leb128 (curr, & bytes_read, 0);
	  curr += bytes_read;
	  string = (char *) curr;
	  curr += strlen (string) + 1;
	  printf (_(" DW_MACINFO_define - lineno : %d macro : %s\n"),
		  lineno, string);
	  break;

	case DW_MACINFO_undef:
	  lineno = read_leb128 (curr, & bytes_read, 0);
	  curr += bytes_read;
	  string = (char *) curr;
	  curr += strlen (string) + 1;
	  printf (_(" DW_MACINFO_undef - lineno : %d macro : %s\n"),
		  lineno, string);
	  break;

	case DW_MACINFO_vendor_ext:
	  {
	    unsigned int constant;

	    constant = read_leb128 (curr, & bytes_read, 0);
	    curr += bytes_read;
	    string = (char *) curr;
	    curr += strlen (string) + 1;
	    printf (_(" DW_MACINFO_vendor_ext - constant : %d string : %s\n"),
		    constant, string);
	  }
	  break;
	}
    }

  return 1;
}

static int
display_debug_abbrev (struct dwarf_section *section,
		      void *file ATTRIBUTE_UNUSED)
{
  abbrev_entry *entry;
  unsigned char *start = section->start;
  unsigned char *end = start + section->size;

  printf (_("Contents of the %s section:\n\n"), section->name);

  do
    {
      free_abbrevs ();

      start = process_abbrev_section (start, end);

      if (first_abbrev == NULL)
	continue;

      printf (_("  Number TAG\n"));

      for (entry = first_abbrev; entry; entry = entry->next)
	{
	  abbrev_attr *attr;

	  printf (_("   %ld      %s    [%s]\n"),
		  entry->entry,
		  get_TAG_name (entry->tag),
		  entry->children ? _("has children") : _("no children"));

	  for (attr = entry->first_attr; attr; attr = attr->next)
	    printf (_("    %-18s %s\n"),
		    get_AT_name (attr->attribute),
		    get_FORM_name (attr->form));
	}
    }
  while (start);

  printf ("\n");

  return 1;
}

static int
display_debug_loc (struct dwarf_section *section, void *file)
{
  unsigned char *start = section->start;
  unsigned char *section_end;
  unsigned long bytes;
  unsigned char *section_begin = start;
  unsigned int num_loc_list = 0;
  unsigned long last_offset = 0;
  unsigned int first = 0;
  unsigned int i;
  unsigned int j;
  int seen_first_offset = 0;
  int use_debug_info = 1;
  unsigned char *next;

  bytes = section->size;
  section_end = start + bytes;

  if (bytes == 0)
    {
      printf (_("\nThe %s section is empty.\n"), section->name);
      return 0;
    }

  if (load_debug_info (file) == 0)
    {
      warn (_("Unable to load/parse the .debug_info section, so cannot interpret the %s section.\n"),
	    section->name);
      return 0;
    }

  /* Check the order of location list in .debug_info section. If
     offsets of location lists are in the ascending order, we can
     use `debug_information' directly.  */
  for (i = 0; i < num_debug_info_entries; i++)
    {
      unsigned int num;

      num = debug_information [i].num_loc_offsets;
      num_loc_list += num;

      /* Check if we can use `debug_information' directly.  */
      if (use_debug_info && num != 0)
	{
	  if (!seen_first_offset)
	    {
	      /* This is the first location list.  */
	      last_offset = debug_information [i].loc_offsets [0];
	      first = i;
	      seen_first_offset = 1;
	      j = 1;
	    }
	  else
	    j = 0;

	  for (; j < num; j++)
	    {
	      if (last_offset >
		  debug_information [i].loc_offsets [j])
		{
		  use_debug_info = 0;
		  break;
		}
	      last_offset = debug_information [i].loc_offsets [j];
	    }
	}
    }

  if (!use_debug_info)
    /* FIXME: Should we handle this case?  */
    error (_("Location lists in .debug_info section aren't in ascending order!\n"));

  if (!seen_first_offset)
    error (_("No location lists in .debug_info section!\n"));

  /* DWARF sections under Mach-O have non-zero addresses.  */
  if (debug_information [first].num_loc_offsets > 0
      && debug_information [first].loc_offsets [0] != section->address)
    warn (_("Location lists in %s section start at 0x%lx\n"),
	  section->name, debug_information [first].loc_offsets [0]);

  printf (_("Contents of the %s section:\n\n"), section->name);
  printf (_("    Offset   Begin    End      Expression\n"));

  seen_first_offset = 0;
  for (i = first; i < num_debug_info_entries; i++)
    {
      dwarf_vma begin;
      dwarf_vma end;
      unsigned short length;
      unsigned long offset;
      unsigned int pointer_size;
      unsigned long cu_offset;
      unsigned long base_address;
      int need_frame_base;
      int has_frame_base;

      pointer_size = debug_information [i].pointer_size;
      cu_offset = debug_information [i].cu_offset;

      for (j = 0; j < debug_information [i].num_loc_offsets; j++)
	{
	  has_frame_base = debug_information [i].have_frame_base [j];
	  /* DWARF sections under Mach-O have non-zero addresses.  */
	  offset = debug_information [i].loc_offsets [j] - section->address;
	  next = section_begin + offset;
	  base_address = debug_information [i].base_address;

	  if (!seen_first_offset)
	    seen_first_offset = 1;
	  else
	    {
	      if (start < next)
		warn (_("There is a hole [0x%lx - 0x%lx] in .debug_loc section.\n"),
		      (unsigned long) (start - section_begin),
		      (unsigned long) (next - section_begin));
	      else if (start > next)
		warn (_("There is an overlap [0x%lx - 0x%lx] in .debug_loc section.\n"),
		      (unsigned long) (start - section_begin),
		      (unsigned long) (next - section_begin));
	    }
	  start = next;

	  if (offset >= bytes)
	    {
	      warn (_("Offset 0x%lx is bigger than .debug_loc section size.\n"),
		    offset);
	      continue;
	    }

	  while (1)
	    {
	      if (start + 2 * pointer_size > section_end)
		{
		  warn (_("Location list starting at offset 0x%lx is not terminated.\n"),
			offset);
		  break;
		}

	      /* Note: we use sign extension here in order to be sure that
		 we can detect the -1 escape value.  Sign extension into the
		 top 32 bits of a 32-bit address will not affect the values
		 that we display since we always show hex values, and always
		 the bottom 32-bits.  */
	      begin = byte_get_signed (start, pointer_size);
	      start += pointer_size;
	      end = byte_get_signed (start, pointer_size);
	      start += pointer_size;

	      printf ("    %8.8lx ", offset);

	      if (begin == 0 && end == 0)
		{
		  printf (_("<End of list>\n"));
		  break;
		}

	      /* Check base address specifiers.  */
	      if (begin == (dwarf_vma) -1 && end != (dwarf_vma) -1)
		{
		  base_address = end;
		  print_dwarf_vma (begin, pointer_size);
		  print_dwarf_vma (end, pointer_size);
		  printf (_("(base address)\n"));
		  continue;
		}

	      if (start + 2 > section_end)
		{
		  warn (_("Location list starting at offset 0x%lx is not terminated.\n"),
			offset);
		  break;
		}

	      length = byte_get (start, 2);
	      start += 2;

	      if (start + length > section_end)
		{
		  warn (_("Location list starting at offset 0x%lx is not terminated.\n"),
			offset);
		  break;
		}

	      print_dwarf_vma (begin + base_address, pointer_size);
	      print_dwarf_vma (end + base_address, pointer_size);

	      putchar ('(');
	      need_frame_base = decode_location_expression (start,
							    pointer_size,
							    length,
							    cu_offset, section);
	      putchar (')');

	      if (need_frame_base && !has_frame_base)
		printf (_(" [without DW_AT_frame_base]"));

	      if (begin == end)
		fputs (_(" (start == end)"), stdout);
	      else if (begin > end)
		fputs (_(" (start > end)"), stdout);

	      putchar ('\n');

	      start += length;
	    }
	}
    }

  if (start < section_end)
    warn (_("There are %ld unused bytes at the end of section %s\n"),
	  (long) (section_end - start), section->name);
  putchar ('\n');
  return 1;
}

static int
display_debug_str (struct dwarf_section *section,
		   void *file ATTRIBUTE_UNUSED)
{
  unsigned char *start = section->start;
  unsigned long bytes = section->size;
  dwarf_vma addr = section->address;

  if (bytes == 0)
    {
      printf (_("\nThe %s section is empty.\n"), section->name);
      return 0;
    }

  printf (_("Contents of the %s section:\n\n"), section->name);

  while (bytes)
    {
      int j;
      int k;
      int lbytes;

      lbytes = (bytes > 16 ? 16 : bytes);

      printf ("  0x%8.8lx ", (unsigned long) addr);

      for (j = 0; j < 16; j++)
	{
	  if (j < lbytes)
	    printf ("%2.2x", start[j]);
	  else
	    printf ("  ");

	  if ((j & 3) == 3)
	    printf (" ");
	}

      for (j = 0; j < lbytes; j++)
	{
	  k = start[j];
	  if (k >= ' ' && k < 0x80)
	    printf ("%c", k);
	  else
	    printf (".");
	}

      putchar ('\n');

      start += lbytes;
      addr  += lbytes;
      bytes -= lbytes;
    }

  putchar ('\n');

  return 1;
}

static int
display_debug_info (struct dwarf_section *section, void *file)
{
  return process_debug_info (section, file, 0, 0);
}

static int
display_debug_types (struct dwarf_section *section, void *file)
{
  return process_debug_info (section, file, 0, 1);
}

static int
display_debug_aranges (struct dwarf_section *section,
		       void *file ATTRIBUTE_UNUSED)
{
  unsigned char *start = section->start;
  unsigned char *end = start + section->size;

  printf (_("Contents of the %s section:\n\n"), section->name);

  /* It does not matter if this load fails,
     we test for that later on.  */
  load_debug_info (file);

  while (start < end)
    {
      unsigned char *hdrptr;
      DWARF2_Internal_ARange arange;
      unsigned char *ranges;
      dwarf_vma length;
      dwarf_vma address;
      unsigned char address_size;
      int excess;
      int offset_size;
      int initial_length_size;

      hdrptr = start;

      arange.ar_length = byte_get (hdrptr, 4);
      hdrptr += 4;

      if (arange.ar_length == 0xffffffff)
	{
	  arange.ar_length = byte_get (hdrptr, 8);
	  hdrptr += 8;
	  offset_size = 8;
	  initial_length_size = 12;
	}
      else
	{
	  offset_size = 4;
	  initial_length_size = 4;
	}

      arange.ar_version = byte_get (hdrptr, 2);
      hdrptr += 2;

      arange.ar_info_offset = byte_get (hdrptr, offset_size);
      hdrptr += offset_size;

      if (num_debug_info_entries != DEBUG_INFO_UNAVAILABLE
	  && num_debug_info_entries > 0
	  && find_debug_info_for_offset (arange.ar_info_offset) == NULL)
	warn (_(".debug_info offset of 0x%lx in %s section does not point to a CU header.\n"),
	      arange.ar_info_offset, section->name);

      arange.ar_pointer_size = byte_get (hdrptr, 1);
      hdrptr += 1;

      arange.ar_segment_size = byte_get (hdrptr, 1);
      hdrptr += 1;

      if (arange.ar_version != 2 && arange.ar_version != 3)
	{
	  warn (_("Only DWARF 2 and 3 aranges are currently supported.\n"));
	  break;
	}

      printf (_("  Length:                   %ld\n"), arange.ar_length);
      printf (_("  Version:                  %d\n"), arange.ar_version);
      printf (_("  Offset into .debug_info:  0x%lx\n"), arange.ar_info_offset);
      printf (_("  Pointer Size:             %d\n"), arange.ar_pointer_size);
      printf (_("  Segment Size:             %d\n"), arange.ar_segment_size);

      address_size = arange.ar_pointer_size + arange.ar_segment_size;

      /* The DWARF spec does not require that the address size be a power
	 of two, but we do.  This will have to change if we ever encounter
	 an uneven architecture.  */
      if ((address_size & (address_size - 1)) != 0)
	{
	  warn (_("Pointer size + Segment size is not a power of two.\n"));
	  break;
	}

      if (address_size > 4)
	printf (_("\n    Address            Length\n"));
      else
	printf (_("\n    Address    Length\n"));

      ranges = hdrptr;

      /* Must pad to an alignment boundary that is twice the address size.  */
      excess = (hdrptr - start) % (2 * address_size);
      if (excess)
	ranges += (2 * address_size) - excess;

      start += arange.ar_length + initial_length_size;

      while (ranges + 2 * address_size <= start)
	{
	  address = byte_get (ranges, address_size);

	  ranges += address_size;

	  length  = byte_get (ranges, address_size);

	  ranges += address_size;

	  printf ("    ");
	  print_dwarf_vma (address, address_size);
	  print_dwarf_vma (length, address_size);
	  putchar ('\n');
	}
    }

  printf ("\n");

  return 1;
}

/* Each debug_information[x].range_lists[y] gets this representation for
   sorting purposes.  */

struct range_entry
  {
    /* The debug_information[x].range_lists[y] value.  */
    unsigned long ranges_offset;

    /* Original debug_information to find parameters of the data.  */
    debug_info *debug_info_p;
  };

/* Sort struct range_entry in ascending order of its RANGES_OFFSET.  */

static int
range_entry_compar (const void *ap, const void *bp)
{
  const struct range_entry *a_re = (const struct range_entry *) ap;
  const struct range_entry *b_re = (const struct range_entry *) bp;
  const unsigned long a = a_re->ranges_offset;
  const unsigned long b = b_re->ranges_offset;

  return (a > b) - (b > a);
}

static int
display_debug_ranges (struct dwarf_section *section,
		      void *file ATTRIBUTE_UNUSED)
{
  unsigned char *start = section->start;
  unsigned char *section_end;
  unsigned long bytes;
  unsigned char *section_begin = start;
  unsigned int num_range_list, i;
  struct range_entry *range_entries, *range_entry_fill;

  bytes = section->size;
  section_end = start + bytes;

  if (bytes == 0)
    {
      printf (_("\nThe %s section is empty.\n"), section->name);
      return 0;
    }

  if (load_debug_info (file) == 0)
    {
      warn (_("Unable to load/parse the .debug_info section, so cannot interpret the %s section.\n"),
	    section->name);
      return 0;
    }

  num_range_list = 0;
  for (i = 0; i < num_debug_info_entries; i++)
    num_range_list += debug_information [i].num_range_lists;

  if (num_range_list == 0)
    error (_("No range lists in .debug_info section!\n"));

  range_entries = (struct range_entry *)
      xmalloc (sizeof (*range_entries) * num_range_list);
  range_entry_fill = range_entries;

  for (i = 0; i < num_debug_info_entries; i++)
    {
      debug_info *debug_info_p = &debug_information[i];
      unsigned int j;

      for (j = 0; j < debug_info_p->num_range_lists; j++)
	{
	  range_entry_fill->ranges_offset = debug_info_p->range_lists[j];
	  range_entry_fill->debug_info_p = debug_info_p;
	  range_entry_fill++;
	}
    }

  qsort (range_entries, num_range_list, sizeof (*range_entries),
	 range_entry_compar);

  /* DWARF sections under Mach-O have non-zero addresses.  */
  if (range_entries[0].ranges_offset != section->address)
    warn (_("Range lists in %s section start at 0x%lx\n"),
	  section->name, range_entries[0].ranges_offset);

  printf (_("Contents of the %s section:\n\n"), section->name);
  printf (_("    Offset   Begin    End\n"));

  for (i = 0; i < num_range_list; i++)
    {
      struct range_entry *range_entry = &range_entries[i];
      debug_info *debug_info_p = range_entry->debug_info_p;
      unsigned int pointer_size;
      unsigned long offset;
      unsigned char *next;
      unsigned long base_address;

      pointer_size = debug_info_p->pointer_size;

      /* DWARF sections under Mach-O have non-zero addresses.  */
      offset = range_entry->ranges_offset - section->address;
      next = section_begin + offset;
      base_address = debug_info_p->base_address;

      if (i > 0)
	{
	  if (start < next)
	    warn (_("There is a hole [0x%lx - 0x%lx] in %s section.\n"),
		  (unsigned long) (start - section_begin),
		  (unsigned long) (next - section_begin), section->name);
	  else if (start > next)
	    warn (_("There is an overlap [0x%lx - 0x%lx] in %s section.\n"),
		  (unsigned long) (start - section_begin),
		  (unsigned long) (next - section_begin), section->name);
	}
      start = next;

      while (1)
	{
	  dwarf_vma begin;
	  dwarf_vma end;

	  /* Note: we use sign extension here in order to be sure that
	     we can detect the -1 escape value.  Sign extension into the
	     top 32 bits of a 32-bit address will not affect the values
	     that we display since we always show hex values, and always
	     the bottom 32-bits.  */
	  begin = byte_get_signed (start, pointer_size);
	  start += pointer_size;
	  end = byte_get_signed (start, pointer_size);
	  start += pointer_size;

	  printf ("    %8.8lx ", offset);

	  if (begin == 0 && end == 0)
	    {
	      printf (_("<End of list>\n"));
	      break;
	    }

	  /* Check base address specifiers.  */
	  if (begin == (dwarf_vma) -1 && end != (dwarf_vma) -1)
	    {
	      base_address = end;
	      print_dwarf_vma (begin, pointer_size);
	      print_dwarf_vma (end, pointer_size);
	      printf ("(base address)\n");
	      continue;
	    }

	  print_dwarf_vma (begin + base_address, pointer_size);
	  print_dwarf_vma (end + base_address, pointer_size);

	  if (begin == end)
	    fputs (_("(start == end)"), stdout);
	  else if (begin > end)
	    fputs (_("(start > end)"), stdout);

	  putchar ('\n');
	}
    }
  putchar ('\n');

  free (range_entries);

  return 1;
}

typedef struct Frame_Chunk
{
  struct Frame_Chunk *next;
  unsigned char *chunk_start;
  int ncols;
  /* DW_CFA_{undefined,same_value,offset,register,unreferenced}  */
  short int *col_type;
  int *col_offset;
  char *augmentation;
  unsigned int code_factor;
  int data_factor;
  unsigned long pc_begin;
  unsigned long pc_range;
  int cfa_reg;
  int cfa_offset;
  int ra;
  unsigned char fde_encoding;
  unsigned char cfa_exp;
}
Frame_Chunk;

static const char *const *dwarf_regnames;
static unsigned int dwarf_regnames_count;

/* A marker for a col_type that means this column was never referenced
   in the frame info.  */
#define DW_CFA_unreferenced (-1)

/* Return 0 if not more space is needed, 1 if more space is needed,
   -1 for invalid reg.  */

static int
frame_need_space (Frame_Chunk *fc, unsigned int reg)
{
  int prev = fc->ncols;

  if (reg < (unsigned int) fc->ncols)
    return 0;

  if (dwarf_regnames_count
      && reg > dwarf_regnames_count)
    return -1;

  fc->ncols = reg + 1;
  fc->col_type = (short int *) xcrealloc (fc->col_type, fc->ncols,
                                          sizeof (short int));
  fc->col_offset = (int *) xcrealloc (fc->col_offset, fc->ncols, sizeof (int));

  while (prev < fc->ncols)
    {
      fc->col_type[prev] = DW_CFA_unreferenced;
      fc->col_offset[prev] = 0;
      prev++;
    }
  return 1;
}

static const char *const dwarf_regnames_i386[] =
{
  "eax", "ecx", "edx", "ebx",
  "esp", "ebp", "esi", "edi",
  "eip", "eflags", NULL,
  "st0", "st1", "st2", "st3",
  "st4", "st5", "st6", "st7",
  NULL, NULL,
  "xmm0", "xmm1", "xmm2", "xmm3",
  "xmm4", "xmm5", "xmm6", "xmm7",
  "mm0", "mm1", "mm2", "mm3",
  "mm4", "mm5", "mm6", "mm7",
  "fcw", "fsw", "mxcsr",
  "es", "cs", "ss", "ds", "fs", "gs", NULL, NULL,
  "tr", "ldtr"
};

static const char *const dwarf_regnames_x86_64[] =
{
  "rax", "rdx", "rcx", "rbx",
  "rsi", "rdi", "rbp", "rsp",
  "r8",  "r9",  "r10", "r11",
  "r12", "r13", "r14", "r15",
  "rip",
  "xmm0",  "xmm1",  "xmm2",  "xmm3",
  "xmm4",  "xmm5",  "xmm6",  "xmm7",
  "xmm8",  "xmm9",  "xmm10", "xmm11",
  "xmm12", "xmm13", "xmm14", "xmm15",
  "st0", "st1", "st2", "st3",
  "st4", "st5", "st6", "st7",
  "mm0", "mm1", "mm2", "mm3",
  "mm4", "mm5", "mm6", "mm7",
  "rflags",
  "es", "cs", "ss", "ds", "fs", "gs", NULL, NULL,
  "fs.base", "gs.base", NULL, NULL,
  "tr", "ldtr",
  "mxcsr", "fcw", "fsw"
};

void
init_dwarf_regnames (unsigned int e_machine)
{
  switch (e_machine)
    {
    case EM_386:
    case EM_486:
      dwarf_regnames = dwarf_regnames_i386;
      dwarf_regnames_count = ARRAY_SIZE (dwarf_regnames_i386);
      break;

    case EM_X86_64:
      dwarf_regnames = dwarf_regnames_x86_64;
      dwarf_regnames_count = ARRAY_SIZE (dwarf_regnames_x86_64);
      break;

    default:
      break;
    }
}

static const char *
regname (unsigned int regno, int row)
{
  static char reg[64];
  if (dwarf_regnames
      && regno < dwarf_regnames_count
      && dwarf_regnames [regno] != NULL)
    {
      if (row)
	return dwarf_regnames [regno];
      snprintf (reg, sizeof (reg), "r%d (%s)", regno,
		dwarf_regnames [regno]);
    }
  else
    snprintf (reg, sizeof (reg), "r%d", regno);
  return reg;
}

static void
frame_display_row (Frame_Chunk *fc, int *need_col_headers, int *max_regs)
{
  int r;
  char tmp[100];

  if (*max_regs < fc->ncols)
    *max_regs = fc->ncols;

  if (*need_col_headers)
    {
      static const char *loc = "   LOC";

      *need_col_headers = 0;

      printf ("%-*s CFA      ", eh_addr_size * 2, loc);

      for (r = 0; r < *max_regs; r++)
	if (fc->col_type[r] != DW_CFA_unreferenced)
	  {
	    if (r == fc->ra)
	      printf ("ra      ");
	    else
	      printf ("%-5s ", regname (r, 1));
	  }

      printf ("\n");
    }

  printf ("%0*lx ", eh_addr_size * 2, fc->pc_begin);
  if (fc->cfa_exp)
    strcpy (tmp, "exp");
  else
    sprintf (tmp, "%s%+d", regname (fc->cfa_reg, 1), fc->cfa_offset);
  printf ("%-8s ", tmp);

  for (r = 0; r < fc->ncols; r++)
    {
      if (fc->col_type[r] != DW_CFA_unreferenced)
	{
	  switch (fc->col_type[r])
	    {
	    case DW_CFA_undefined:
	      strcpy (tmp, "u");
	      break;
	    case DW_CFA_same_value:
	      strcpy (tmp, "s");
	      break;
	    case DW_CFA_offset:
	      sprintf (tmp, "c%+d", fc->col_offset[r]);
	      break;
	    case DW_CFA_val_offset:
	      sprintf (tmp, "v%+d", fc->col_offset[r]);
	      break;
	    case DW_CFA_register:
	      sprintf (tmp, "%s", regname (fc->col_offset[r], 0));
	      break;
	    case DW_CFA_expression:
	      strcpy (tmp, "exp");
	      break;
	    case DW_CFA_val_expression:
	      strcpy (tmp, "vexp");
	      break;
	    default:
	      strcpy (tmp, "n/a");
	      break;
	    }
	  printf ("%-5s ", tmp);
	}
    }
  printf ("\n");
}

#define GET(N)	byte_get (start, N); start += N
#define LEB()	read_leb128 (start, & length_return, 0); start += length_return
#define SLEB()	read_leb128 (start, & length_return, 1); start += length_return

static int
display_debug_frames (struct dwarf_section *section,
		      void *file ATTRIBUTE_UNUSED)
{
  unsigned char *start = section->start;
  unsigned char *end = start + section->size;
  unsigned char *section_start = start;
  Frame_Chunk *chunks = 0;
  Frame_Chunk *remembered_state = 0;
  Frame_Chunk *rs;
  int is_eh = strcmp (section->name, ".eh_frame") == 0;
  unsigned int length_return;
  int max_regs = 0;
  const char *bad_reg = _("bad register: ");

  printf (_("Contents of the %s section:\n"), section->name);

  while (start < end)
    {
      unsigned char *saved_start;
      unsigned char *block_end;
      unsigned long length;
      unsigned long cie_id;
      Frame_Chunk *fc;
      Frame_Chunk *cie;
      int need_col_headers = 1;
      unsigned char *augmentation_data = NULL;
      unsigned long augmentation_data_len = 0;
      int encoded_ptr_size = eh_addr_size;
      int offset_size;
      int initial_length_size;

      saved_start = start;
      length = byte_get (start, 4); start += 4;

      if (length == 0)
	{
	  printf ("\n%08lx ZERO terminator\n\n",
		    (unsigned long)(saved_start - section_start));
	  continue;
	}

      if (length == 0xffffffff)
	{
	  length = byte_get (start, 8);
	  start += 8;
	  offset_size = 8;
	  initial_length_size = 12;
	}
      else
	{
	  offset_size = 4;
	  initial_length_size = 4;
	}

      block_end = saved_start + length + initial_length_size;
      if (block_end > end)
	{
	  warn ("Invalid length %#08lx in FDE at %#08lx\n",
		length, (unsigned long)(saved_start - section_start));
	  block_end = end;
	}
      cie_id = byte_get (start, offset_size); start += offset_size;

      if (is_eh ? (cie_id == 0) : (cie_id == DW_CIE_ID))
	{
	  int version;

	  fc = (Frame_Chunk *) xmalloc (sizeof (Frame_Chunk));
	  memset (fc, 0, sizeof (Frame_Chunk));

	  fc->next = chunks;
	  chunks = fc;
	  fc->chunk_start = saved_start;
	  fc->ncols = 0;
	  fc->col_type = (short int *) xmalloc (sizeof (short int));
	  fc->col_offset = (int *) xmalloc (sizeof (int));
	  frame_need_space (fc, max_regs - 1);

	  version = *start++;

	  fc->augmentation = (char *) start;
	  start = (unsigned char *) strchr ((char *) start, '\0') + 1;

	  if (fc->augmentation[0] == 'z')
	    {
	      fc->code_factor = LEB ();
	      fc->data_factor = SLEB ();
	      if (version == 1)
		{
		  fc->ra = GET (1);
		}
	      else
		{
		  fc->ra = LEB ();
		}
	      augmentation_data_len = LEB ();
	      augmentation_data = start;
	      start += augmentation_data_len;
	    }
	  else if (strcmp (fc->augmentation, "eh") == 0)
	    {
	      start += eh_addr_size;
	      fc->code_factor = LEB ();
	      fc->data_factor = SLEB ();
	      if (version == 1)
		{
		  fc->ra = GET (1);
		}
	      else
		{
		  fc->ra = LEB ();
		}
	    }
	  else
	    {
	      fc->code_factor = LEB ();
	      fc->data_factor = SLEB ();
	      if (version == 1)
		{
		  fc->ra = GET (1);
		}
	      else
		{
		  fc->ra = LEB ();
		}
	    }
	  cie = fc;

	  if (do_debug_frames_interp)
	    printf ("\n%08lx %08lx %08lx CIE \"%s\" cf=%d df=%d ra=%d\n",
		    (unsigned long)(saved_start - section_start), length, cie_id,
		    fc->augmentation, fc->code_factor, fc->data_factor,
		    fc->ra);
	  else
	    {
	      printf ("\n%08lx %08lx %08lx CIE\n",
		      (unsigned long)(saved_start - section_start), length, cie_id);
	      printf ("  Version:               %d\n", version);
	      printf ("  Augmentation:          \"%s\"\n", fc->augmentation);
	      printf ("  Code alignment factor: %u\n", fc->code_factor);
	      printf ("  Data alignment factor: %d\n", fc->data_factor);
	      printf ("  Return address column: %d\n", fc->ra);

	      if (augmentation_data_len)
		{
		  unsigned long i;
		  printf ("  Augmentation data:    ");
		  for (i = 0; i < augmentation_data_len; ++i)
		    printf (" %02x", augmentation_data[i]);
		  putchar ('\n');
		}
	      putchar ('\n');
	    }

	  if (augmentation_data_len)
	    {
	      unsigned char *p, *q;
	      p = (unsigned char *) fc->augmentation + 1;
	      q = augmentation_data;

	      while (1)
		{
		  if (*p == 'L')
		    q++;
		  else if (*p == 'P')
		    q += 1 + size_of_encoded_value (*q);
		  else if (*p == 'R')
		    fc->fde_encoding = *q++;
		  else
		    break;
		  p++;
		}

	      if (fc->fde_encoding)
		encoded_ptr_size = size_of_encoded_value (fc->fde_encoding);
	    }

	  frame_need_space (fc, fc->ra);
	}
      else
	{
	  unsigned char *look_for;
	  static Frame_Chunk fde_fc;

	  fc = & fde_fc;
	  memset (fc, 0, sizeof (Frame_Chunk));

	  look_for = is_eh ? start - 4 - cie_id : section_start + cie_id;

	  for (cie = chunks; cie ; cie = cie->next)
	    if (cie->chunk_start == look_for)
	      break;

	  if (!cie)
	    {
	      warn ("Invalid CIE pointer %#08lx in FDE at %#08lx\n",
		    cie_id, (unsigned long)(saved_start - section_start));
	      fc->ncols = 0;
	      fc->col_type = (short int *) xmalloc (sizeof (short int));
	      fc->col_offset = (int *) xmalloc (sizeof (int));
	      frame_need_space (fc, max_regs - 1);
	      cie = fc;
	      fc->augmentation = "";
	      fc->fde_encoding = 0;
	    }
	  else
	    {
	      fc->ncols = cie->ncols;
	      fc->col_type = (short int *) xcmalloc (fc->ncols, sizeof (short int));
	      fc->col_offset =  (int *) xcmalloc (fc->ncols, sizeof (int));
	      memcpy (fc->col_type, cie->col_type, fc->ncols * sizeof (short int));
	      memcpy (fc->col_offset, cie->col_offset, fc->ncols * sizeof (int));
	      fc->augmentation = cie->augmentation;
	      fc->code_factor = cie->code_factor;
	      fc->data_factor = cie->data_factor;
	      fc->cfa_reg = cie->cfa_reg;
	      fc->cfa_offset = cie->cfa_offset;
	      fc->ra = cie->ra;
	      frame_need_space (fc, max_regs - 1);
	      fc->fde_encoding = cie->fde_encoding;
	    }

	  if (fc->fde_encoding)
	    encoded_ptr_size = size_of_encoded_value (fc->fde_encoding);

	  fc->pc_begin = get_encoded_value (start, fc->fde_encoding);
	  if ((fc->fde_encoding & 0x70) == DW_EH_PE_pcrel)
	    fc->pc_begin += section->address + (start - section_start);
	  start += encoded_ptr_size;
	  fc->pc_range = byte_get (start, encoded_ptr_size);
	  start += encoded_ptr_size;

	  if (cie->augmentation[0] == 'z')
	    {
	      augmentation_data_len = LEB ();
	      augmentation_data = start;
	      start += augmentation_data_len;
	    }

	  printf ("\n%08lx %08lx %08lx FDE cie=%08lx pc=%08lx..%08lx\n",
		  (unsigned long)(saved_start - section_start), length, cie_id,
		  (unsigned long)(cie->chunk_start - section_start),
		  fc->pc_begin, fc->pc_begin + fc->pc_range);
	  if (! do_debug_frames_interp && augmentation_data_len)
	    {
	      unsigned long i;

	      printf ("  Augmentation data:    ");
	      for (i = 0; i < augmentation_data_len; ++i)
		printf (" %02x", augmentation_data[i]);
	      putchar ('\n');
	      putchar ('\n');
	    }
	}

      /* At this point, fc is the current chunk, cie (if any) is set, and
	 we're about to interpret instructions for the chunk.  */
      /* ??? At present we need to do this always, since this sizes the
	 fc->col_type and fc->col_offset arrays, which we write into always.
	 We should probably split the interpreted and non-interpreted bits
	 into two different routines, since there's so much that doesn't
	 really overlap between them.  */
      if (1 || do_debug_frames_interp)
	{
	  /* Start by making a pass over the chunk, allocating storage
	     and taking note of what registers are used.  */
	  unsigned char *tmp = start;

	  while (start < block_end)
	    {
	      unsigned op, opa;
	      unsigned long reg, tmp;

	      op = *start++;
	      opa = op & 0x3f;
	      if (op & 0xc0)
		op &= 0xc0;

	      /* Warning: if you add any more cases to this switch, be
		 sure to add them to the corresponding switch below.  */
	      switch (op)
		{
		case DW_CFA_advance_loc:
		  break;
		case DW_CFA_offset:
		  LEB ();
		  if (frame_need_space (fc, opa) >= 0)
		    fc->col_type[opa] = DW_CFA_undefined;
		  break;
		case DW_CFA_restore:
		  if (frame_need_space (fc, opa) >= 0)
		    fc->col_type[opa] = DW_CFA_undefined;
		  break;
		case DW_CFA_set_loc:
		  start += encoded_ptr_size;
		  break;
		case DW_CFA_advance_loc1:
		  start += 1;
		  break;
		case DW_CFA_advance_loc2:
		  start += 2;
		  break;
		case DW_CFA_advance_loc4:
		  start += 4;
		  break;
		case DW_CFA_offset_extended:
		case DW_CFA_val_offset:
		  reg = LEB (); LEB ();
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_restore_extended:
		  reg = LEB ();
		  frame_need_space (fc, reg);
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_undefined:
		  reg = LEB ();
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_same_value:
		  reg = LEB ();
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_register:
		  reg = LEB (); LEB ();
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_def_cfa:
		  LEB (); LEB ();
		  break;
		case DW_CFA_def_cfa_register:
		  LEB ();
		  break;
		case DW_CFA_def_cfa_offset:
		  LEB ();
		  break;
		case DW_CFA_def_cfa_expression:
		  tmp = LEB ();
		  start += tmp;
		  break;
		case DW_CFA_expression:
		case DW_CFA_val_expression:
		  reg = LEB ();
		  tmp = LEB ();
		  start += tmp;
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_offset_extended_sf:
		case DW_CFA_val_offset_sf:
		  reg = LEB (); SLEB ();
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		case DW_CFA_def_cfa_sf:
		  LEB (); SLEB ();
		  break;
		case DW_CFA_def_cfa_offset_sf:
		  SLEB ();
		  break;
		case DW_CFA_MIPS_advance_loc8:
		  start += 8;
		  break;
		case DW_CFA_GNU_args_size:
		  LEB ();
		  break;
		case DW_CFA_GNU_negative_offset_extended:
		  reg = LEB (); LEB ();
		  if (frame_need_space (fc, reg) >= 0)
		    fc->col_type[reg] = DW_CFA_undefined;
		  break;
		default:
		  break;
		}
	    }
	  start = tmp;
	}

      /* Now we know what registers are used, make a second pass over
	 the chunk, this time actually printing out the info.  */

      while (start < block_end)
	{
	  unsigned op, opa;
	  unsigned long ul, reg, roffs;
	  long l, ofs;
	  dwarf_vma vma;
	  const char *reg_prefix = "";

	  op = *start++;
	  opa = op & 0x3f;
	  if (op & 0xc0)
	    op &= 0xc0;

	  /* Warning: if you add any more cases to this switch, be
	     sure to add them to the corresponding switch above.  */
	  switch (op)
	    {
	    case DW_CFA_advance_loc:
	      if (do_debug_frames_interp)
		frame_display_row (fc, &need_col_headers, &max_regs);
	      else
		printf ("  DW_CFA_advance_loc: %d to %08lx\n",
			opa * fc->code_factor,
			fc->pc_begin + opa * fc->code_factor);
	      fc->pc_begin += opa * fc->code_factor;
	      break;

	    case DW_CFA_offset:
	      roffs = LEB ();
	      if (opa >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_offset: %s%s at cfa%+ld\n",
			reg_prefix, regname (opa, 0),
			roffs * fc->data_factor);
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[opa] = DW_CFA_offset;
		  fc->col_offset[opa] = roffs * fc->data_factor;
		}
	      break;

	    case DW_CFA_restore:
	      if (opa >= (unsigned int) cie->ncols
		  || opa >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_restore: %s%s\n",
			reg_prefix, regname (opa, 0));
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[opa] = cie->col_type[opa];
		  fc->col_offset[opa] = cie->col_offset[opa];
		}
	      break;

	    case DW_CFA_set_loc:
	      vma = get_encoded_value (start, fc->fde_encoding);
	      if ((fc->fde_encoding & 0x70) == DW_EH_PE_pcrel)
		vma += section->address + (start - section_start);
	      start += encoded_ptr_size;
	      if (do_debug_frames_interp)
		frame_display_row (fc, &need_col_headers, &max_regs);
	      else
		printf ("  DW_CFA_set_loc: %08lx\n", (unsigned long)vma);
	      fc->pc_begin = vma;
	      break;

	    case DW_CFA_advance_loc1:
	      ofs = byte_get (start, 1); start += 1;
	      if (do_debug_frames_interp)
		frame_display_row (fc, &need_col_headers, &max_regs);
	      else
		printf ("  DW_CFA_advance_loc1: %ld to %08lx\n",
			ofs * fc->code_factor,
			fc->pc_begin + ofs * fc->code_factor);
	      fc->pc_begin += ofs * fc->code_factor;
	      break;

	    case DW_CFA_advance_loc2:
	      ofs = byte_get (start, 2); start += 2;
	      if (do_debug_frames_interp)
		frame_display_row (fc, &need_col_headers, &max_regs);
	      else
		printf ("  DW_CFA_advance_loc2: %ld to %08lx\n",
			ofs * fc->code_factor,
			fc->pc_begin + ofs * fc->code_factor);
	      fc->pc_begin += ofs * fc->code_factor;
	      break;

	    case DW_CFA_advance_loc4:
	      ofs = byte_get (start, 4); start += 4;
	      if (do_debug_frames_interp)
		frame_display_row (fc, &need_col_headers, &max_regs);
	      else
		printf ("  DW_CFA_advance_loc4: %ld to %08lx\n",
			ofs * fc->code_factor,
			fc->pc_begin + ofs * fc->code_factor);
	      fc->pc_begin += ofs * fc->code_factor;
	      break;

	    case DW_CFA_offset_extended:
	      reg = LEB ();
	      roffs = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_offset_extended: %s%s at cfa%+ld\n",
			reg_prefix, regname (reg, 0),
			roffs * fc->data_factor);
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_offset;
		  fc->col_offset[reg] = roffs * fc->data_factor;
		}
	      break;

	    case DW_CFA_val_offset:
	      reg = LEB ();
	      roffs = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_val_offset: %s%s at cfa%+ld\n",
			reg_prefix, regname (reg, 0),
			roffs * fc->data_factor);
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_val_offset;
		  fc->col_offset[reg] = roffs * fc->data_factor;
		}
	      break;

	    case DW_CFA_restore_extended:
	      reg = LEB ();
	      if (reg >= (unsigned int) cie->ncols
		  || reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_restore_extended: %s%s\n",
			reg_prefix, regname (reg, 0));
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = cie->col_type[reg];
		  fc->col_offset[reg] = cie->col_offset[reg];
		}
	      break;

	    case DW_CFA_undefined:
	      reg = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_undefined: %s%s\n",
			reg_prefix, regname (reg, 0));
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_undefined;
		  fc->col_offset[reg] = 0;
		}
	      break;

	    case DW_CFA_same_value:
	      reg = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_same_value: %s%s\n",
			reg_prefix, regname (reg, 0));
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_same_value;
		  fc->col_offset[reg] = 0;
		}
	      break;

	    case DW_CFA_register:
	      reg = LEB ();
	      roffs = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		{
		  printf ("  DW_CFA_register: %s%s in ",
			  reg_prefix, regname (reg, 0));
		  puts (regname (roffs, 0));
		}
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_register;
		  fc->col_offset[reg] = roffs;
		}
	      break;

	    case DW_CFA_remember_state:
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_remember_state\n");
	      rs = (Frame_Chunk *) xmalloc (sizeof (Frame_Chunk));
	      rs->ncols = fc->ncols;
	      rs->col_type = (short int *) xcmalloc (rs->ncols,
                                                     sizeof (short int));
	      rs->col_offset = (int *) xcmalloc (rs->ncols, sizeof (int));
	      memcpy (rs->col_type, fc->col_type, rs->ncols);
	      memcpy (rs->col_offset, fc->col_offset, rs->ncols * sizeof (int));
	      rs->next = remembered_state;
	      remembered_state = rs;
	      break;

	    case DW_CFA_restore_state:
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_restore_state\n");
	      rs = remembered_state;
	      if (rs)
		{
		  remembered_state = rs->next;
		  frame_need_space (fc, rs->ncols - 1);
		  memcpy (fc->col_type, rs->col_type, rs->ncols);
		  memcpy (fc->col_offset, rs->col_offset,
			  rs->ncols * sizeof (int));
		  free (rs->col_type);
		  free (rs->col_offset);
		  free (rs);
		}
	      else if (do_debug_frames_interp)
		printf ("Mismatched DW_CFA_restore_state\n");
	      break;

	    case DW_CFA_def_cfa:
	      fc->cfa_reg = LEB ();
	      fc->cfa_offset = LEB ();
	      fc->cfa_exp = 0;
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_def_cfa: %s ofs %d\n",
			regname (fc->cfa_reg, 0), fc->cfa_offset);
	      break;

	    case DW_CFA_def_cfa_register:
	      fc->cfa_reg = LEB ();
	      fc->cfa_exp = 0;
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_def_cfa_register: %s\n",
			regname (fc->cfa_reg, 0));
	      break;

	    case DW_CFA_def_cfa_offset:
	      fc->cfa_offset = LEB ();
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_def_cfa_offset: %d\n", fc->cfa_offset);
	      break;

	    case DW_CFA_nop:
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_nop\n");
	      break;

	    case DW_CFA_def_cfa_expression:
	      ul = LEB ();
	      if (! do_debug_frames_interp)
		{
		  printf ("  DW_CFA_def_cfa_expression (");
		  decode_location_expression (start, eh_addr_size, ul, 0,
					      section);
		  printf (")\n");
		}
	      fc->cfa_exp = 1;
	      start += ul;
	      break;

	    case DW_CFA_expression:
	      reg = LEB ();
	      ul = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		{
		  printf ("  DW_CFA_expression: %s%s (",
			  reg_prefix, regname (reg, 0));
		  decode_location_expression (start, eh_addr_size,
					      ul, 0, section);
		  printf (")\n");
		}
	      if (*reg_prefix == '\0')
		fc->col_type[reg] = DW_CFA_expression;
	      start += ul;
	      break;

	    case DW_CFA_val_expression:
	      reg = LEB ();
	      ul = LEB ();
	      if (reg >= (unsigned int) fc->ncols)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		{
		  printf ("  DW_CFA_val_expression: %s%s (",
			  reg_prefix, regname (reg, 0));
		  decode_location_expression (start, eh_addr_size, ul, 0,
					      section);
		  printf (")\n");
		}
	      if (*reg_prefix == '\0')
		fc->col_type[reg] = DW_CFA_val_expression;
	      start += ul;
	      break;

	    case DW_CFA_offset_extended_sf:
	      reg = LEB ();
	      l = SLEB ();
	      if (frame_need_space (fc, reg) < 0)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_offset_extended_sf: %s%s at cfa%+ld\n",
			reg_prefix, regname (reg, 0),
			l * fc->data_factor);
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_offset;
		  fc->col_offset[reg] = l * fc->data_factor;
		}
	      break;

	    case DW_CFA_val_offset_sf:
	      reg = LEB ();
	      l = SLEB ();
	      if (frame_need_space (fc, reg) < 0)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_val_offset_sf: %s%s at cfa%+ld\n",
			reg_prefix, regname (reg, 0),
			l * fc->data_factor);
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_val_offset;
		  fc->col_offset[reg] = l * fc->data_factor;
		}
	      break;

	    case DW_CFA_def_cfa_sf:
	      fc->cfa_reg = LEB ();
	      fc->cfa_offset = SLEB ();
	      fc->cfa_offset = fc->cfa_offset * fc->data_factor;
	      fc->cfa_exp = 0;
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_def_cfa_sf: %s ofs %d\n",
			regname (fc->cfa_reg, 0), fc->cfa_offset);
	      break;

	    case DW_CFA_def_cfa_offset_sf:
	      fc->cfa_offset = SLEB ();
	      fc->cfa_offset = fc->cfa_offset * fc->data_factor;
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_def_cfa_offset_sf: %d\n", fc->cfa_offset);
	      break;

	    case DW_CFA_MIPS_advance_loc8:
	      ofs = byte_get (start, 8); start += 8;
	      if (do_debug_frames_interp)
		frame_display_row (fc, &need_col_headers, &max_regs);
	      else
		printf ("  DW_CFA_MIPS_advance_loc8: %ld to %08lx\n",
			ofs * fc->code_factor,
			fc->pc_begin + ofs * fc->code_factor);
	      fc->pc_begin += ofs * fc->code_factor;
	      break;

	    case DW_CFA_GNU_window_save:
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_GNU_window_save\n");
	      break;

	    case DW_CFA_GNU_args_size:
	      ul = LEB ();
	      if (! do_debug_frames_interp)
		printf ("  DW_CFA_GNU_args_size: %ld\n", ul);
	      break;

	    case DW_CFA_GNU_negative_offset_extended:
	      reg = LEB ();
	      l = - LEB ();
	      if (frame_need_space (fc, reg) < 0)
		reg_prefix = bad_reg;
	      if (! do_debug_frames_interp || *reg_prefix != '\0')
		printf ("  DW_CFA_GNU_negative_offset_extended: %s%s at cfa%+ld\n",
			reg_prefix, regname (reg, 0),
			l * fc->data_factor);
	      if (*reg_prefix == '\0')
		{
		  fc->col_type[reg] = DW_CFA_offset;
		  fc->col_offset[reg] = l * fc->data_factor;
		}
	      break;

	    default:
	      if (op >= DW_CFA_lo_user && op <= DW_CFA_hi_user)
		printf (_("  DW_CFA_??? (User defined call frame op: %#x)\n"), op);
	      else
		warn (_("unsupported or unknown Dwarf Call Frame Instruction number: %#x\n"), op);
	      start = block_end;
	    }
	}

      if (do_debug_frames_interp)
	frame_display_row (fc, &need_col_headers, &max_regs);

      start = block_end;
    }

  printf ("\n");

  return 1;
}

#undef GET
#undef LEB
#undef SLEB

static int
display_debug_not_supported (struct dwarf_section *section,
			     void *file ATTRIBUTE_UNUSED)
{
  printf (_("Displaying the debug contents of section %s is not yet supported.\n"),
	    section->name);

  return 1;
}

void *
cmalloc (size_t nmemb, size_t size)
{
  /* Check for overflow.  */
  if (nmemb >= ~(size_t) 0 / size)
    return NULL;
  else
    return malloc (nmemb * size);
}

void *
xcmalloc (size_t nmemb, size_t size)
{
  /* Check for overflow.  */
  if (nmemb >= ~(size_t) 0 / size)
    return NULL;
  else
    return xmalloc (nmemb * size);
}

void *
xcrealloc (void *ptr, size_t nmemb, size_t size)
{
  /* Check for overflow.  */
  if (nmemb >= ~(size_t) 0 / size)
    return NULL;
  else
    return xrealloc (ptr, nmemb * size);
}

void
error (const char *message, ...)
{
  va_list args;

  va_start (args, message);
  fprintf (stderr, _("%s: Error: "), program_name);
  vfprintf (stderr, message, args);
  va_end (args);
}

void
warn (const char *message, ...)
{
  va_list args;

  va_start (args, message);
  fprintf (stderr, _("%s: Warning: "), program_name);
  vfprintf (stderr, message, args);
  va_end (args);
}

void
free_debug_memory (void)
{
  unsigned int i;

  free_abbrevs ();

  for (i = 0; i < max; i++)
    free_debug_section ((enum dwarf_section_display_enum) i);

  if (debug_information != NULL)
    {
      if (num_debug_info_entries != DEBUG_INFO_UNAVAILABLE)
	{
	  for (i = 0; i < num_debug_info_entries; i++)
	    {
	      if (!debug_information [i].max_loc_offsets)
		{
		  free (debug_information [i].loc_offsets);
		  free (debug_information [i].have_frame_base);
		}
	      if (!debug_information [i].max_range_lists)
		free (debug_information [i].range_lists);
	    }
	}

      free (debug_information);
      debug_information = NULL;
      num_debug_info_entries = 0;
    }
}

void
dwarf_select_sections_by_names (const char *names)
{
  typedef struct
  {
    const char * option;
    int *        variable;
    int val;
  }
  debug_dump_long_opts;

  static const debug_dump_long_opts opts_table [] =
    {
      /* Please keep this table alpha- sorted.  */
      { "Ranges", & do_debug_ranges, 1 },
      { "abbrev", & do_debug_abbrevs, 1 },
      { "aranges", & do_debug_aranges, 1 },
      { "frames", & do_debug_frames, 1 },
      { "frames-interp", & do_debug_frames_interp, 1 },
      { "info", & do_debug_info, 1 },
      { "line", & do_debug_lines, FLAG_DEBUG_LINES_RAW }, /* For backwards compatibility.  */
      { "rawline", & do_debug_lines, FLAG_DEBUG_LINES_RAW },
      { "decodedline", & do_debug_lines, FLAG_DEBUG_LINES_DECODED },
      { "loc",  & do_debug_loc, 1 },
      { "macro", & do_debug_macinfo, 1 },
      { "pubnames", & do_debug_pubnames, 1 },
      /* This entry is for compatability
	 with earlier versions of readelf.  */
      { "ranges", & do_debug_aranges, 1 },
      { "str", & do_debug_str, 1 },
      { NULL, NULL, 0 }
    };

  const char *p;
  
  p = names;
  while (*p)
    {
      const debug_dump_long_opts * entry;
      
      for (entry = opts_table; entry->option; entry++)
	{
	  size_t len = strlen (entry->option);
	  
	  if (strncmp (p, entry->option, len) == 0
	      && (p[len] == ',' || p[len] == '\0'))
	    {
	      * entry->variable |= entry->val;
	      
	      /* The --debug-dump=frames-interp option also
		 enables the --debug-dump=frames option.  */
	      if (do_debug_frames_interp)
		do_debug_frames = 1;

	      p += len;
	      break;
	    }
	}
      
      if (entry->option == NULL)
	{
	  warn (_("Unrecognized debug option '%s'\n"), p);
	  p = strchr (p, ',');
	  if (p == NULL)
	    break;
	}
      
      if (*p == ',')
	p++;
    }
}

void
dwarf_select_sections_by_letters (const char *letters)
{
  unsigned int index = 0;

  while (letters[index])
    switch (letters[index++])
      {
      case 'i':
	do_debug_info = 1;
	break;
	
      case 'a':
	do_debug_abbrevs = 1;
	break;
	
      case 'l':
	do_debug_lines |= FLAG_DEBUG_LINES_RAW;
	break;
	
      case 'L':
	do_debug_lines |= FLAG_DEBUG_LINES_DECODED;
	break;
	
      case 'p':
	do_debug_pubnames = 1;
	break;
	
      case 'r':
	do_debug_aranges = 1;
	break;
	
      case 'R':
	do_debug_ranges = 1;
	break;
	
      case 'F':
	do_debug_frames_interp = 1;
      case 'f':
	do_debug_frames = 1;
	break;
	
      case 'm':
	do_debug_macinfo = 1;
	break;
	
      case 's':
	do_debug_str = 1;
	break;
	
      case 'o':
	do_debug_loc = 1;
	break;
	
      default:
	warn (_("Unrecognized debug option '%s'\n"), optarg);
	break;
      }
}

void
dwarf_select_sections_all (void)
{
  do_debug_info = 1;
  do_debug_abbrevs = 1;
  do_debug_lines = FLAG_DEBUG_LINES_RAW;
  do_debug_pubnames = 1;
  do_debug_aranges = 1;
  do_debug_ranges = 1;
  do_debug_frames = 1;
  do_debug_macinfo = 1;
  do_debug_str = 1;
  do_debug_loc = 1;
}

struct dwarf_section_display debug_displays[] =
{
  { { ".debug_abbrev",		".zdebug_abbrev",	NULL,	NULL,	0,	0 },
    display_debug_abbrev,		&do_debug_abbrevs,	0 },
  { { ".debug_aranges",		".zdebug_aranges",	NULL,	NULL,	0,	0 },
    display_debug_aranges,		&do_debug_aranges,	1 },
  { { ".debug_frame",		".zdebug_frame",	NULL,	NULL,	0,	0 },
    display_debug_frames,		&do_debug_frames,	1 },
  { { ".debug_info",		".zdebug_info",		NULL,	NULL,	0,	0 },
    display_debug_info,			&do_debug_info,		1 },
  { { ".debug_line",		".zdebug_line",		NULL,	NULL,	0,	0 },
    display_debug_lines,		&do_debug_lines,	1 },
  { { ".debug_pubnames",	".zdebug_pubnames",	NULL,	NULL,	0,	0 },
    display_debug_pubnames,		&do_debug_pubnames,	0 },
  { { ".eh_frame",		"",			NULL,	NULL,	0,	0 },
    display_debug_frames,		&do_debug_frames,	1 },
  { { ".debug_macinfo",		".zdebug_macinfo",	NULL,	NULL,	0,	0 },
    display_debug_macinfo,		&do_debug_macinfo,	0 },
  { { ".debug_str",		".zdebug_str",		NULL,	NULL,	0,	0 },
    display_debug_str,			&do_debug_str,		0 },
  { { ".debug_loc",		".zdebug_loc",		NULL,	NULL,	0,	0 },
    display_debug_loc,			&do_debug_loc,		1 },
  { { ".debug_pubtypes",	".zdebug_pubtypes",	NULL,	NULL,	0,	0 },
    display_debug_pubnames,		&do_debug_pubnames,	0 },
  { { ".debug_ranges",		".zdebug_ranges",	NULL,	NULL,	0,	0 },
    display_debug_ranges,		&do_debug_ranges,	1 },
  { { ".debug_static_func",	".zdebug_static_func",	NULL,	NULL,	0,	0 },
    display_debug_not_supported,	NULL,			0 },
  { { ".debug_static_vars",	".zdebug_static_vars",	NULL,	NULL,	0,	0 },
    display_debug_not_supported,	NULL,			0 },
  { { ".debug_types",		".zdebug_types",	NULL,	NULL,	0,	0 },
    display_debug_types,		&do_debug_info,		1 },
  { { ".debug_weaknames",	".zdebug_weaknames",	NULL,	NULL,	0,	0 },
    display_debug_not_supported,	NULL,			0 }
};
