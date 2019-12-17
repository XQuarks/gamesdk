package com.google.tfmonitor

import androidx.recyclerview.widget.RecyclerView
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import com.google.protobuf.DescriptorProtos


import com.google.tfmonitor.FieldItemFragment.OnListFragmentInteractionListener

import kotlinx.android.synthetic.main.fragment_item.view.*

class FieldItemRecyclerViewAdapter(
    private val mValues: List<DescriptorProtos.FieldDescriptorProto>,
    private val mListener: OnListFragmentInteractionListener?
) : RecyclerView.Adapter<FieldItemRecyclerViewAdapter.ViewHolder>() {

    private val mOnClickListener: View.OnClickListener

    init {
        mOnClickListener = View.OnClickListener { v ->
            val item = v.tag as DescriptorProtos.FieldDescriptorProto
            // Notify the active callbacks interface (the activity, if the fragment is attached to
            // one) that an item has been selected.
            mListener?.onListFragmentInteraction(item)
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.fragment_item, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val item = mValues[position]
        holder.mNameView.text = item.name
        holder.mTypeView.text = item.typeName

        with(holder.mView) {
            tag = item
            setOnClickListener(mOnClickListener)
        }
    }

    override fun getItemCount(): Int = mValues.size

    inner class ViewHolder(val mView: View) : RecyclerView.ViewHolder(mView) {
        val mNameView: TextView = mView.item_name
        val mTypeView: TextView = mView.item_type
        val mValueView: View? = null

        override fun toString(): String {
            return super.toString() + " '" + mNameView.toString() + "'"
        }
    }
}
