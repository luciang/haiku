//----------------------------------------------------------------------------
// Anti-Grain Geometry - Version 2.2
// Copyright (C) 2002-2004 Maxim Shemanarev (http://www.antigrain.com)
//
// Permission to copy, use, modify, sell and distribute this software 
// is granted provided this copyright notice appears in all copies. 
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
//----------------------------------------------------------------------------
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://www.antigrain.com
//----------------------------------------------------------------------------
//
// class rendering_buffer
//
//----------------------------------------------------------------------------

#ifndef AGG_RENDERING_BUFFER_INCLUDED
#define AGG_RENDERING_BUFFER_INCLUDED

#include "agg_basics.h"

namespace agg
{

    //==========================================================row_ptr_cache
    template<class T> class row_ptr_cache
    {
    public:
        //-------------------------------------------------------------------
        ~row_ptr_cache()
        {
            delete [] m_rows;
        }

        //-------------------------------------------------------------------
        row_ptr_cache() :
            m_buf(0),
            m_rows(0),
            m_width(0),
            m_height(0),
            m_stride(0),
            m_max_height(0)
        {
        }

        //--------------------------------------------------------------------
        row_ptr_cache(T* buf, unsigned width, unsigned height, int stride) :
            m_buf(0),
            m_rows(0),
            m_width(0),
            m_height(0),
            m_stride(0),
            m_max_height(0)
        {
            attach(buf, width, height, stride);
        }

        //--------------------------------------------------------------------
        void attach(T* buf, unsigned width, unsigned height, int stride)
        {
            m_buf = buf;
            m_width = width;
            m_height = height;
            m_stride = stride;
            if(height > m_max_height)
            {
                delete [] m_rows;
                m_rows = new T* [m_max_height = height];
            }

            T* row_ptr = m_buf;

            if(stride < 0)
            {
                row_ptr = m_buf - int(height - 1) * stride;
            }

            T** rows = m_rows;

            while(height--)
            {
                *rows++ = row_ptr;
                row_ptr += stride;
            }
        }

        //--------------------------------------------------------------------
        const T* buf()    const { return m_buf;    }
        unsigned width()  const { return m_width;  }
        unsigned height() const { return m_height; }
        int      stride() const { return m_stride; }
        unsigned stride_abs() const 
        {
            return (m_stride < 0) ? 
                unsigned(-m_stride) : 
                unsigned(m_stride); 
        }

        //--------------------------------------------------------------------
        T* row(unsigned y) { return m_rows[y]; }
        const T* row(unsigned y) const { return m_rows[y]; }
        T const* const* rows() const { return m_rows; }

        //--------------------------------------------------------------------
        void copy_from(const row_ptr_cache<T>& mtx)
        {
            unsigned h = height();
            if(mtx.height() < h) h = mtx.height();
        
            unsigned l = stride_abs();
            if(mtx.stride_abs() < l) l = mtx.stride_abs();
        
            l *= sizeof(T);

            unsigned y;
            for (y = 0; y < h; y++)
            {
                memcpy(row(y), mtx.row(y), l);
            }
        }

    private:
        //--------------------------------------------------------------------
        // Prohibit copying
        row_ptr_cache(const row_ptr_cache<T>&);
        const row_ptr_cache<T>& operator = (const row_ptr_cache<T>&);

    private:
        //--------------------------------------------------------------------
        T*       m_buf;        // Pointer to renrdering buffer
        T**      m_rows;       // Pointers to each row of the buffer
        unsigned m_width;      // Width in pixels
        unsigned m_height;     // Height in pixels
        int      m_stride;     // Number of bytes per row. Can be < 0
        unsigned m_max_height; // The maximal height (currently allocated)
    };



    //========================================================rendering_buffer
    typedef row_ptr_cache<int8u> rendering_buffer;

}


#endif
