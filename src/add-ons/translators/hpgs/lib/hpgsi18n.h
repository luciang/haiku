/***********************************************************************
 *                                                                     *
 * $Id: hpgsi18n.h 270 2006-01-29 21:12:23Z softadm $
 *                                                                     *
 * hpgs - HPGl Script, a hpgl/2 interpreter, which uses a Postscript   *
 *        API for rendering a scene and thus renders to a variety of   *
 *        devices and fileformats.                                     *
 *                                                                     *
 * (C) 2004-2006 ev-i Informationstechnologie GmbH  http://www.ev-i.at *
 *                                                                     *
 * Author: Wolfgang Glas                                               *
 *                                                                     *
 *  hpgs is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU Lesser General Public          *
 * License as published by the Free Software Foundation; either        *
 * version 2.1 of the License, or (at your option) any later version.  *
 *                                                                     *
 * hpgs is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   *
 * Lesser General Public License for more details.                     *
 *                                                                     *
 * You should have received a copy of the GNU Lesser General Public    *
 * License along with this library; if not, write to the               *
 * Free Software  Foundation, Inc., 59 Temple Place, Suite 330,        *
 * Boston, MA  02111-1307  USA                                         *
 *                                                                     *
 ***********************************************************************
 *                                                                     *
 * Private subroutines used by i18n facilities of the library.         *
 *                                                                     *
 ***********************************************************************/

#ifndef __HPGS_I18N_H__
#define __HPGS_I18N_H__

#include <hpgs.h>

#ifdef HPGS_HAVE_GETTEXT

#include <libintl.h>

HPGS_INTERNAL_API void hpgs_i18n_init();
#endif // HPGS_HAVE_GETTEXT

#endif // ! __HPGS_I18N_H__
