import React from 'react'
import TopPanel from './screen_components/TopPanel.tsx'
import Board from './screen_components/Board.tsx'
import Credits from './screen_components/Credits.tsx'

const Screen = () => {
  return (
    <div>
        <TopPanel/>

        <Board/>

        <Credits/>
    </div>
  )
}

export default Screen